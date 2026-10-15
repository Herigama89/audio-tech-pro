#include "ThemePanel.h"
#include "ThemePack.h"

namespace riff
{

ThemePanel::ThemePanel (theme::Settings initial, std::function<void (const theme::Settings&)> callback, juce::PropertiesFile* presetStore)
    : settings (initial), onChange (std::move (callback)), store (presetStore)
{
    skinBox.addItemList ({ "Studio Grey (from your picture)", "Flat Dark (FL Studio style)", "Studio White (your fader, knob and meter photos)",
                           "Xenon (your dark glass console picture)", "Pro Tools style", "Ableton style",
                           "REAPER theme (choose or load one below)" }, 1);
    skinBox.onChange = [this]
    {
        settings.skin = (theme::Skin) juce::jlimit (0, (int) theme::Skin::reaper, skinBox.getSelectedItemIndex());
        if (settings.skin == theme::Skin::reaper && settings.themePack.isEmpty())
            if (const auto themes = themepack::installed(); ! themes.isEmpty())
                settings.themePack = themes.getFirst().getFullPathName();
        changed();
        refreshReaperThemes();
    };
    addAndMakeVisible (skinBox);

    loadReaperButton.setTooltip ("Use a REAPER theme (.ReaperThemeZip): its pictures (buttons, faders, knobs, slots) and colours skin the app. It's copied into the app's own folder.");
    loadReaperButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load a REAPER theme", juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*.ReaperThemeZip;*.ReaperTheme;*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<ThemePanel> (this)] (const juce::FileChooser& c)
        {
            if (safe == nullptr || c.getResult() == juce::File())
                return;
            juce::String error;
            const auto folder = themepack::install (c.getResult(), error);
            if (folder == juce::File())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load the theme", error);
                return;
            }
            safe->settings.skin = theme::Skin::reaper;
            safe->settings.themePack = folder.getFullPathName();
            safe->refreshControls();
            safe->changed();
            safe->refreshReaperThemes();
        });
    };
    addAndMakeVisible (loadReaperButton);
    reaperBox.setTextWhenNothingSelected ("REAPER themes loaded");
    reaperBox.setTooltip ("The REAPER themes you've loaded: choose one to use it");
    reaperBox.onChange = [this]
    {
        const auto themes = themepack::installed();
        const auto index = reaperBox.getSelectedItemIndex();
        if (! juce::isPositiveAndBelow (index, themes.size()))
            return;
        settings.skin = theme::Skin::reaper;
        settings.themePack = themes[index].getFullPathName();
        refreshControls();
        changed();
    };
    addAndMakeVisible (reaperBox);
    refreshReaperThemes();

    faderBox.addItemList ({ "Skin's own", "Classic", "Studio White (photo cap)", "Xenon (glass cap)", "REAPER theme's" }, 1);
    faderBox.setTooltip ("The fader caps, whatever the skin: mix the Xenon look with Studio White faders, say");
    faderBox.onChange = [this]
    {
        settings.faderStyle = faderBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (faderBox);

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
        { "Studio White",  { theme::Skin::white, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f } },
        { "Xenon",         { theme::Skin::xenon, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f } },
        { "Pro Tools",     { theme::Skin::protools, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f } },
        { "Ableton",       { theme::Skin::ableton, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f } },
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

    // Meter colours
    const char* meterNames[] { "Low", "Mid", "Peak" };
    for (int i = 0; i < 3; ++i)
    {
        meterButtons[i].setButtonText (meterNames[i]);
        meterButtons[i].setTooltip (juce::String ("The meters' ") + (i == 0 ? "colour for normal levels" : i == 1 ? "colour near the top" : "colour at 0 dB (clipping)")
                                    + ". Click to choose.");
        meterButtons[i].onClick = [this, i] { chooseMeterColour (i); };
        addAndMakeVisible (meterButtons[i]);
    }
    meterDefaultButton.setTooltip ("Back to the skin's own meter colours");
    meterDefaultButton.onClick = [this]
    {
        settings.meterLow = settings.meterMid = settings.meterPeak = 0;
        changed();
    };
    addAndMakeVisible (meterDefaultButton);

    // Waveforms and regions
    waveModeBox.addItemList ({ "Skin's own", "Track colour", "One colour", "Gradient" }, 1);
    waveModeBox.setTooltip ("How waveforms are coloured: the track's colour, one colour of your own, or a gradient across each region");
    waveModeBox.onChange = [this]
    {
        settings.waveMode = waveModeBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (waveModeBox);
    const char* waveNames[] { "From", "To" };
    for (int i = 0; i < 2; ++i)
    {
        waveButtons[i].setButtonText (waveNames[i]);
        waveButtons[i].setTooltip (i == 0 ? "The waveform's colour (with Gradient, its colour at the left)" : "With Gradient: the waveform's colour at the right");
        waveButtons[i].onClick = [this, i] { chooseMeterColour (3 + i); };
        addAndMakeVisible (waveButtons[i]);
    }
    regionBox.addItemList ({ "Skin's own", "Filled", "Glass" }, 1);
    const char* markNames[] { "Locators", "Markers" };
    for (int i = 0; i < 2; ++i)
    {
        markButtons[i].setButtonText (markNames[i]);
        markButtons[i].setTooltip (i == 0 ? "The cycle's colour in the ruler (Logic's locators)" : "The markers' colour in the marker track");
        markButtons[i].onClick = [this, i] { chooseMeterColour (5 + i); };
        addAndMakeVisible (markButtons[i]);
    }
    markDefaultButton.setTooltip ("Back to the skin's own locator and marker colours");
    markDefaultButton.onClick = [this]
    {
        settings.locatorColour = settings.markerColour = 0;
        changed();
    };
    addAndMakeVisible (markDefaultButton);

    const char* editNames[] { "    Tracks", "    Bars", "    Beats", "    Roll" };   // (room for the colour chip)
    const char* editTips[] { "The tracks area's background (its lanes)", "The bar lines in the tracks area and the editors",
                             "The beat lines", "The piano roll's rows (the black-key rows a shade darker)" };
    for (int i = 0; i < 4; ++i)
    {
        editButtons[i].setButtonText (editNames[i]);
        editButtons[i].setTooltip (editTips[i]);
        editButtons[i].onClick = [this, i] { chooseMeterColour (7 + i); };
        addAndMakeVisible (editButtons[i]);
    }
    editDefaultButton.setTooltip ("Back to the skin's own edit window colours");
    editDefaultButton.onClick = [this]
    {
        settings.editBack = settings.gridBarColour = settings.gridBeatColour = settings.rollBack = 0;
        changed();
    };
    addAndMakeVisible (editDefaultButton);
    regionBox.setTooltip ("Filled: regions in the track's colour. Glass: dark see-through regions with a lit outline.");
    regionBox.onChange = [this]
    {
        settings.glassRegions = regionBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (regionBox);

    // Your presets
    if (store != nullptr)
    {
        myPresets.setTextWhenNothingSelected ("My presets");
        myPresets.onChange = [this]
        {
            const auto all = loadPresets();
            const auto name = myPresets.getText();
            if (all.containsKey (name))
            {
                settings = theme::Settings::fromString (all[name]);
                refreshControls();
                changed();
            }
        };
        savePreset.onClick = [this]
        {
            nameWindow = std::make_unique<juce::AlertWindow> ("Save Theme Preset", "A name for these colours:", juce::MessageBoxIconType::NoIcon);
            nameWindow->addTextEditor ("name", myPresets.getText().isNotEmpty() ? myPresets.getText() : juce::String ("My Theme"));
            nameWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ThemePanel> (this)] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;
                const auto name = safe->nameWindow->getTextEditorContents ("name").trim().replaceCharacter ('\n', ' ');
                if (name.isEmpty())
                    return;
                auto all = safe->loadPresets();
                all.set (name, safe->settings.toString());   // (the same name replaces it)
                safe->savePresets (all);
                safe->refreshMyPresets();
                safe->myPresets.setText (name, juce::dontSendNotification);
            }), false);
        };
        deletePreset.onClick = [this]
        {
            auto all = loadPresets();
            all.remove (juce::StringRef (myPresets.getText()));
            savePresets (all);
            refreshMyPresets();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &myPresets, &savePreset, &deletePreset })
            addAndMakeVisible (c);
        refreshMyPresets();
    }

    refreshControls();
    setSize (460, 120 + (int) rows.size() * 34 + 110 + 40 + 68 + 34 + 30 + 34 + 34 + 34 + (store != nullptr ? 36 : 0));
}

void ThemePanel::chooseMeterColour (int which)
{
    // A colour picker beside the button; the meters change as you pick.
    const auto current = colourNow (which);

    struct Picker final : juce::ColourSelector, juce::ChangeListener
    {
        explicit Picker (std::function<void (juce::Colour)> cb)
            : juce::ColourSelector (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders | juce::ColourSelector::showColourAtTop),
              onPick (std::move (cb))
        {
            addChangeListener (this);
            setSize (280, 300);
        }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (onPick) onPick (getCurrentColour()); }
        std::function<void (juce::Colour)> onPick;
    };
    auto picker = std::make_unique<Picker> ([safe = juce::Component::SafePointer<ThemePanel> (this), which] (juce::Colour c)
    {
        if (safe == nullptr)
            return;
        safe->colourSlot (which) = c.withAlpha (1.0f).getARGB();
        safe->changed();
    });
    picker->setCurrentColour (current, juce::dontSendNotification);
    juce::CallOutBox::launchAsynchronously (std::move (picker), colourButton (which).getScreenBounds(), nullptr);
}

juce::TextButton& ThemePanel::colourButton (int which)
{
    return which < 3 ? meterButtons[which] : which < 5 ? waveButtons[which - 3] : which < 7 ? markButtons[which - 5] : editButtons[juce::jmin (3, which - 7)];
}

juce::uint32& ThemePanel::colourSlot (int which)
{
    switch (which)
    {
        case 0:  return settings.meterLow;
        case 1:  return settings.meterMid;
        case 2:  return settings.meterPeak;
        case 3:  return settings.waveA;
        case 4:  return settings.waveB;
        case 5:  return settings.locatorColour;
        case 6:  return settings.markerColour;
        case 7:  return settings.editBack;
        case 8:  return settings.gridBarColour;
        case 9:  return settings.gridBeatColour;
        default: return settings.rollBack;
    }
}

juce::Colour ThemePanel::colourNow (int which) const
{
    switch (which)
    {
        case 0:  return theme::meterGreen;
        case 1:  return theme::meterAmber;
        case 2:  return theme::meterRed;
        case 3:  return theme::waveformA();
        case 4:  return theme::waveformB();
        case 5:  return theme::locators;
        case 6:  return theme::markers;
        case 7:  return theme::laneA;
        case 8:  return theme::gridBar;
        case 9:  return theme::gridBeat;
        default: return theme::rollWhite;
    }
}

void ThemePanel::updateMeterButtons()
{
    const juce::Colour colours[] { theme::meterGreen, theme::meterAmber, theme::meterRed };
    for (int i = 0; i < 3; ++i)
    {
        meterButtons[i].setColour (juce::TextButton::buttonColourId, colours[i]);
        meterButtons[i].setColour (juce::TextButton::textColourOffId, colours[i].getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
    }
    for (int i = 0; i < 8; ++i)
    {
        auto& b = colourButton (3 + i);
        const auto c = colourNow (3 + i);
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, c.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white);
    }
}

juce::StringPairArray ThemePanel::loadPresets() const
{
    juce::StringPairArray all;
    if (store != nullptr)
        for (const auto& line : juce::StringArray::fromLines (store->getValue ("themePresets")))
            if (line.contains ("|"))
                all.set (line.upToFirstOccurrenceOf ("|", false, false), line.fromFirstOccurrenceOf ("|", false, false));
    return all;
}

void ThemePanel::savePresets (const juce::StringPairArray& all)
{
    if (store == nullptr)
        return;
    juce::StringArray lines;
    for (const auto& key : all.getAllKeys())
        lines.add (key + "|" + all[key]);
    store->setValue ("themePresets", lines.joinIntoString ("\n"));
    store->saveIfNeeded();
}

void ThemePanel::refreshMyPresets()
{
    myPresets.clear (juce::dontSendNotification);
    const auto all = loadPresets();
    myPresets.addItemList (all.getAllKeys(), 1);
    deletePreset.setEnabled (all.size() > 0);
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
    skinBox.setSelectedItemIndex ((int) settings.skin, juce::dontSendNotification);
    faderBox.setSelectedItemIndex (settings.faderStyle + 1, juce::dontSendNotification);
    refreshReaperThemes();
    waveModeBox.setSelectedItemIndex (settings.waveMode + 1, juce::dontSendNotification);
    regionBox.setSelectedItemIndex (settings.glassRegions + 1, juce::dontSendNotification);
    for (auto& r : rows)
    {
        r.slider->setValue (r.read (settings), juce::dontSendNotification);
        r.slider->updateText();
    }
    ownAccent.setToggleState (settings.accentHue < 0.0f, juce::dontSendNotification);
    updateMeterButtons();
}

void ThemePanel::changed()
{
    if (onChange)
        onChange (settings);
    updateMeterButtons();
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
    g.drawText ("Meter colours", 16, meterButtons[0].getY(), 94, meterButtons[0].getHeight(), juce::Justification::centredLeft);
    g.drawText ("Waveforms", 16, waveModeBox.getY(), 94, waveModeBox.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Regions", 16, regionBox.getY(), 94, regionBox.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Ruler colours", 16, markButtons[0].getY(), 94, markButtons[0].getHeight(), juce::Justification::centredLeft);
    g.drawText ("Faders", 16, faderBox.getY(), 94, faderBox.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Edit windows", 16, editButtons[0].getY(), 94, editButtons[0].getHeight(), juce::Justification::centredLeft);

    // A sample of the colours
    auto sample = juce::Rectangle<int> (16, getHeight() - 92, getWidth() - 32, 28);
    const juce::Colour swatches[] { theme::background, theme::toolbar, theme::panel, theme::stripFace, theme::accent,
                                    theme::meterGreen, theme::meterAmber, theme::meterRed, theme::text };
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

void ThemePanel::paintOverChildren (juce::Graphics& g)
{
    // Each colour as a chip on its button (meters, waveform, locators and markers).
    for (int i = 0; i < 11; ++i)
    {
        const auto& button = colourButton (i);
        const auto chip = button.getBounds().toFloat().reduced (6.0f, 6.0f).removeFromLeft (14.0f);
        g.setColour (colourNow (i));
        g.fillRoundedRectangle (chip, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (chip, 2.0f, 1.0f);
    }
}

void ThemePanel::refreshReaperThemes()
{
    reaperBox.clear (juce::dontSendNotification);
    const auto themes = themepack::installed();
    for (int i = 0; i < themes.size(); ++i)
    {
        reaperBox.addItem (themes[i].getFileName(), i + 1);
        if (settings.skin == theme::Skin::reaper && themes[i].getFullPathName() == settings.themePack)
            reaperBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
}

void ThemePanel::resized()
{
    skinBox.setBounds (110, 14, getWidth() - 126, 24);
    {
        auto row = juce::Rectangle<int> (110, 46, getWidth() - 126, 24);
        loadReaperButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (6);
        reaperBox.setBounds (row);
    }
    auto y = 88;
    for (auto& r : rows)
    {
        r.slider->setBounds (110, y, getWidth() - 126, 26);
        y += 34;
    }
    ownAccent.setBounds (110, y - 4, 220, 22);
    y += 24;
    // The presets in two rows (Reset at the end)
    const auto perRow = (presets.size() + 2) / 2;
    const auto pw = (getWidth() - 32) / perRow;
    for (int i = 0; i <= presets.size(); ++i)
    {
        auto* b = i < presets.size() ? static_cast<juce::Button*> (presets[i]) : &resetButton;
        b->setBounds (16 + (i % perRow) * pw + 2, y + (i / perRow) * 30, pw - 4, 26);
    }
    y += 30;

    y += 34;
    auto meterRow = juce::Rectangle<int> (110, y, getWidth() - 126, 26);
    const auto mw = meterRow.getWidth() / 4;
    for (auto& b : meterButtons)
        b.setBounds (meterRow.removeFromLeft (mw).reduced (2, 0));
    meterDefaultButton.setBounds (meterRow.reduced (2, 0));

    y += 34;
    auto waveRow = juce::Rectangle<int> (110, y, getWidth() - 126, 26);
    waveModeBox.setBounds (waveRow.removeFromLeft (waveRow.getWidth() / 2).reduced (2, 0));
    for (auto& b : waveButtons)
        b.setBounds (waveRow.removeFromLeft (waveRow.getWidth() / (&b == &waveButtons[0] ? 2 : 1)).reduced (2, 0));
    y += 34;
    regionBox.setBounds (112, y, (getWidth() - 126) / 2 - 4, 26);

    y += 34;
    auto markRow = juce::Rectangle<int> (110, y, getWidth() - 126, 26);
    const auto kw = markRow.getWidth() / 3;
    for (auto& b : markButtons)
        b.setBounds (markRow.removeFromLeft (kw).reduced (2, 0));
    markDefaultButton.setBounds (markRow.reduced (2, 0));

    y += 34;
    auto editRow = juce::Rectangle<int> (110, y, getWidth() - 126, 26);
    const auto ew = editRow.getWidth() / 5;
    for (auto& b : editButtons)
        b.setBounds (editRow.removeFromLeft (ew).reduced (2, 0));
    editDefaultButton.setBounds (editRow.reduced (2, 0));

    y += 34;
    faderBox.setBounds (112, y, (getWidth() - 126) / 2 - 4, 26);

    if (store != nullptr)
    {
        y += 36;
        auto mine = juce::Rectangle<int> (16, y, getWidth() - 32, 26);
        deletePreset.setBounds (mine.removeFromRight (70));
        mine.removeFromRight (4);
        savePreset.setBounds (mine.removeFromRight (110));
        mine.removeFromRight (4);
        myPresets.setBounds (mine);
    }
}

} // namespace riff
