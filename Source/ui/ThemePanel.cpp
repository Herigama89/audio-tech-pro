#include "ThemePanel.h"
#include "ThemePack.h"
#include "Looks.h"

namespace riff
{

ThemePanel::ThemePanel (theme::Settings initial, std::function<void (const theme::Settings&)> callback, juce::PropertiesFile* presetStore)
    : settings (initial), whole (initial), onChange (std::move (callback)), store (presetStore)
{
    // Theme for: the mixer (and the rest of the app), or the editor on its own (the tracks area, ruler, editors)
    scopeBox.addItemList ({ "Mixer & app", "Editor (tracks area, editors)", "Toolbar (control bar, transport)", "MultiMeter" }, 1);
    scopeBox.setSelectedItemIndex (0, juce::dontSendNotification);
    scopeBox.setTooltip ("Edit the mixer's theme (and the rest of the app), or give the editor, the toolbar or the MultiMeter a theme of its own");
    scopeBox.onChange = [this] { editScope (scopeBox.getSelectedItemIndex()); };
    addAndMakeVisible (scopeBox);
    sameButton.setTooltip ("This part follows the mixer's theme again");
    sameButton.onClick = [this]
    {
        if (auto* text = scopeText (whole, editingScope))
            *text = {};
        settings = whole;
        settings.editorTheme = settings.toolbarTheme = settings.meterTheme = {};
        refreshControls();
        if (onChange)
            onChange (whole);
        repaint();
    };
    addChildComponent (sameButton);

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

    faderBox.addItemList (looks::faderNames(), 1);
    faderBox.setTooltip ("The fader caps (mixer, track headers, editors), whatever the skin: Xenon with black long-throw caps, say");
    faderBox.onChange = [this]
    {
        settings.faderStyle = faderBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (faderBox);

    knobBox.addItemList (looks::knobNames(), 1);
    knobBox.setTooltip ("The knobs on the channel strips, track headers, plugins' strips and the bus compressor");
    knobBox.onChange = [this]
    {
        settings.knobStyle = knobBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (knobBox);

    buttonBox.addItemList (looks::buttonNames(), 1);
    buttonBox.setTooltip ("The channel keys (mute, solo, record, monitor, EQ and Dynamics in, the bus compressor's IN), lit in their colours");
    buttonBox.onChange = [this]
    {
        settings.buttonStyle = buttonBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (buttonBox);

    meterBox.addItemList (looks::meterNames(), 1);
    meterBox.setTooltip ("The bus compressor's gain reduction meter");
    meterBox.onChange = [this]
    {
        settings.meterStyle = meterBox.getSelectedItemIndex() - 1;
        changed();
    };
    addAndMakeVisible (meterBox);
    stripVuButton.setTooltip ("An amber VU meter at the top of each mixer strip (0 VU at -18 dBFS), as in the Dark VU theme");
    stripVuButton.onClick = [this]
    {
        settings.stripVu = stripVuButton.getToggleState();
        changed();
    };
    addAndMakeVisible (stripVuButton);
    levelMeterBox.addItemList ({ "LED segments", "Smooth (FL Studio style)" }, 1);
    levelMeterBox.setTooltip ("The mixer's and the main window's level meters: lit LED segments, or smooth bars "
                              "shading green to yellow to red with a peak line");
    levelMeterBox.onChange = [this]
    {
        settings.levelMeters = levelMeterBox.getSelectedItemIndex();
        changed();
    };
    addAndMakeVisible (levelMeterBox);

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
        { "Dark VU",       [] {
              // From your mixer picture: dark grey strips, its fader caps, knobs, square keys, green meters and amber VUs
              theme::Settings s;
              s.skin = theme::Skin::flat;
              s.saturation = 0.15f;
              s.lightness = -0.12f;
              s.accentHue = 157.0f;
              s.meterLow = 0xff2bc98fu;
              s.meterMid = 0xffd8c24au;
              s.meterPeak = 0xffe0473au;
              s.faderStyle = 8;
              s.knobStyle = 7;
              s.buttonStyle = 4;
              s.meterStyle = 3;
              s.stripVu = true;
              return s;
          }() },
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

    const char* editNames[] { "Tracks", "Bars", "Beats", "Roll" };   // (the buttons show their colour)
    const char* editTips[] { "The tracks area's background (its lanes)", "The bar lines in the tracks area and the editors",
                             "The beat lines", "The piano roll's rows (the black-key rows a shade darker)" };
    for (int i = 0; i < 4; ++i)
    {
        editButtons[i].setButtonText (editNames[i]);
        editButtons[i].setTooltip (editTips[i]);
        editButtons[i].onClick = [this, i] { chooseMeterColour (7 + i); };
        addAndMakeVisible (editButtons[i]);
    }
    const char* textNames[] { "Text", "Strip text" };
    const char* textTips[] { "The app's text: menus, labels, the toolbar and panels", "The writing on the channel strips and track headers" };
    for (int i = 0; i < 2; ++i)
    {
        textButtons[i].setButtonText (textNames[i]);
        textButtons[i].setTooltip (textTips[i]);
        textButtons[i].onClick = [this, i] { chooseMeterColour (11 + i); };
        addAndMakeVisible (textButtons[i]);
    }
    textDefaultButton.setTooltip ("Back to the skin's own text colours");
    textDefaultButton.onClick = [this]
    {
        settings.textColour = settings.stripTextColour = 0;
        changed();
    };
    addAndMakeVisible (textDefaultButton);

    trackColourBox.addItemList ({ "Track colour: tab", "Track colour: tinted", "Track colour: filled" }, 1);
    trackColourBox.setTooltip ("How much of each track takes its colour: a tab at the edge, the header and mixer strip tinted, "
                               "or filled with it (and the lane washed), as in Cubase, FL Studio and Logic");
    trackColourBox.onChange = [this]
    {
        settings.trackColouring = trackColourBox.getSelectedItemIndex();
        changed();
    };
    addAndMakeVisible (trackColourBox);
    gridStyleBox.addItemList ({ "Grid: lines", "Grid: shaded bars", "Grid: bars only", "Grid: dotted", "Grid: shaded beats (FL)", "Grid: none" }, 1);
    gridStyleBox.setTooltip ("How the grid looks in the tracks area and the editors");
    gridStyleBox.onChange = [this]
    {
        settings.gridStyle = gridStyleBox.getSelectedItemIndex();
        changed();
    };
    addAndMakeVisible (gridStyleBox);

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
        exportButton.setTooltip ("Save these colours and looks as a theme file (.atptheme) to share, or to use on another computer");
        importButton.setTooltip ("Open a theme file someone shared: it's used now and kept in My presets");
        exportButton.onClick = [this] { exportTheme(); };
        importButton.onClick = [this] { importTheme(); };
        for (auto* c : std::initializer_list<juce::Component*> { &myPresets, &savePreset, &deletePreset, &exportButton, &importButton })
            addAndMakeVisible (c);
        refreshMyPresets();
    }

    refreshControls();
    setSize (460, 120 + (int) rows.size() * 34 + 110 + 40 + 68 + 34 + 30 + 34 + 34 + 34 + 34 - 20 + 34 + 68 + 32 + 32 + (store != nullptr ? 36 + 32 : 0));
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
    return which < 3 ? meterButtons[which] : which < 5 ? waveButtons[which - 3] : which < 7 ? markButtons[which - 5]
         : which < 11 ? editButtons[which - 7] : textButtons[juce::jmin (1, which - 11)];
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
        case 11: return settings.textColour;
        case 12: return settings.stripTextColour;
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
        case 11: return theme::text;
        case 12: return theme::stripInk;
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
    for (int i = 0; i < 10; ++i)
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
    knobBox.setSelectedItemIndex (settings.knobStyle + 1, juce::dontSendNotification);
    buttonBox.setSelectedItemIndex (settings.buttonStyle + 1, juce::dontSendNotification);
    meterBox.setSelectedItemIndex (settings.meterStyle + 1, juce::dontSendNotification);
    stripVuButton.setToggleState (settings.stripVu, juce::dontSendNotification);
    levelMeterBox.setSelectedItemIndex (juce::jlimit (0, 1, settings.levelMeters), juce::dontSendNotification);
    refreshReaperThemes();
    waveModeBox.setSelectedItemIndex (settings.waveMode + 1, juce::dontSendNotification);
    regionBox.setSelectedItemIndex (settings.glassRegions + 1, juce::dontSendNotification);
    trackColourBox.setSelectedItemIndex (settings.trackColouring, juce::dontSendNotification);
    gridStyleBox.setSelectedItemIndex (settings.gridStyle, juce::dontSendNotification);
    for (auto& r : rows)
    {
        r.slider->setValue (r.read (settings), juce::dontSendNotification);
        r.slider->updateText();
    }
    ownAccent.setToggleState (settings.accentHue < 0.0f, juce::dontSendNotification);
    updateMeterButtons();
}

juce::String* ThemePanel::scopeText (theme::Settings& s, int scope)
{
    switch (scope)
    {
        case 1: return &s.editorTheme;
        case 2: return &s.toolbarTheme;
        case 3: return &s.meterTheme;
        default: return nullptr;
    }
}

void ThemePanel::editScope (int scope)
{
    editingScope = scope;
    editingEditor = scope == 1;
    if (auto* text = scopeText (whole, scope))
    {
        settings = text->isNotEmpty() ? theme::Settings::fromString (*text) : whole;
        settings.editorTheme = settings.toolbarTheme = settings.meterTheme = {};
    }
    else
        settings = whole;
    sameButton.setVisible (scope != 0);
    stripVuButton.setVisible (scope == 0);
    levelMeterBox.setVisible (scope == 0);
    for (auto* box : { &faderBox, &knobBox, &buttonBox, &meterBox })
        box->setVisible (scope == 0);   // (faders, knobs, keys and the meter are the mixer's)
    refreshControls();
    resized();
    repaint();
}

void ThemePanel::changed()
{
    if (auto* text = scopeText (whole, editingScope))
    {
        auto own = settings;
        own.editorTheme = own.toolbarTheme = own.meterTheme = {};
        *text = own.toString();
    }
    else
    {
        // (the editor's, toolbar's and MultiMeter's own themes stay)
        settings.editorTheme = whole.editorTheme;
        settings.toolbarTheme = whole.toolbarTheme;
        settings.meterTheme = whole.meterTheme;
        whole = settings;
    }
    if (onChange)
        onChange (whole);
    updateMeterButtons();
    repaint();
}

void ThemePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("Theme for", 16, scopeBox.getY(), 100, 24, juce::Justification::centredLeft);
    g.drawText ("Skin", 16, skinBox.getY(), 100, 24, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.5f));
    for (const auto& r : rows)
        g.drawText (r.name, 16, r.slider->getY(), 90, r.slider->getHeight(), juce::Justification::centredLeft);
    g.drawText ("Meter colours", 16, meterButtons[0].getY(), 94, meterButtons[0].getHeight(), juce::Justification::centredLeft);
    g.drawText ("Waveforms", 16, waveModeBox.getY(), 94, waveModeBox.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Regions", 16, regionBox.getY(), 94, regionBox.getHeight(), juce::Justification::centredLeft);
    g.drawText ("Ruler colours", 16, markButtons[0].getY(), 94, markButtons[0].getHeight(), juce::Justification::centredLeft);
    if (editingScope == 0)
    {
        g.drawText ("Faders / Knobs", 16, faderBox.getY(), 94, faderBox.getHeight(), juce::Justification::centredLeft);
        g.drawText ("Keys / Meter", 16, buttonBox.getY(), 94, buttonBox.getHeight(), juce::Justification::centredLeft);
        g.drawText ("Level meters", 16, levelMeterBox.getY(), 94, levelMeterBox.getHeight(), juce::Justification::centredLeft);
    }
    g.drawText ("Edit windows", 16, editButtons[0].getY(), 94, editButtons[0].getHeight(), juce::Justification::centredLeft);
    g.drawText ("Text colours", 16, textButtons[0].getY(), 94, textButtons[0].getHeight(), juce::Justification::centredLeft);
    g.drawText ("Tracks / Grid", 16, trackColourBox.getY(), 94, trackColourBox.getHeight(), juce::Justification::centredLeft);

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
    for (int i = 0; i < 7; ++i)   // (the edit window buttons are filled with their colour instead: they're narrow)
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
    {
        auto scope = juce::Rectangle<int> (110, 14, getWidth() - 126, 24);
        if (sameButton.isVisible())
        {
            sameButton.setBounds (scope.removeFromRight (120));
            scope.removeFromRight (6);
        }
        scopeBox.setBounds (scope);
    }
    skinBox.setBounds (110, 48, getWidth() - 126, 24);
    {
        auto row = juce::Rectangle<int> (110, 80, getWidth() - 126, 24);
        loadReaperButton.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (6);
        reaperBox.setBounds (row);
    }
    auto y = 122;
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
    auto textRow = juce::Rectangle<int> (110, y, getWidth() - 126, 26);
    const auto tw = textRow.getWidth() / 3;
    for (auto& b : textButtons)
        b.setBounds (textRow.removeFromLeft (tw).reduced (2, 0));
    textDefaultButton.setBounds (textRow.reduced (2, 0));

    y += 34;
    {
        const auto halfW = (getWidth() - 126) / 2 - 2;
        trackColourBox.setBounds (112, y, halfW, 26);
        gridStyleBox.setBounds (112 + halfW + 4, y, halfW, 26);
    }

    y += 34;
    const auto half = (getWidth() - 126) / 2 - 2;
    faderBox.setBounds (112, y, half, 26);
    knobBox.setBounds (112 + half + 4, y, half, 26);
    y += 32;
    buttonBox.setBounds (112, y, half, 26);
    meterBox.setBounds (112 + half + 4, y, half, 26);
    y += 32;
    levelMeterBox.setBounds (112, y, half, 26);
    y += 32;
    stripVuButton.setBounds (110, y, getWidth() - 126, 24);

    if (store != nullptr)
    {
        y += 36;
        auto mine = juce::Rectangle<int> (16, y, getWidth() - 32, 26);
        deletePreset.setBounds (mine.removeFromRight (64));
        mine.removeFromRight (4);
        savePreset.setBounds (mine.removeFromRight (104));
        mine.removeFromRight (4);
        myPresets.setBounds (mine);
        y += 32;
        auto files = juce::Rectangle<int> (16, y, getWidth() - 32, 26);
        importButton.setBounds (files.removeFromRight (90));
        files.removeFromRight (4);
        exportButton.setBounds (files.removeFromRight (90));
    }
}

void ThemePanel::exportTheme()
{
    // The whole theme (the main one, with the editor's, the toolbar's and the MultiMeter's inside it)
    const auto name = myPresets.getText().isNotEmpty() ? myPresets.getText() : juce::String ("My Theme");
    chooser = std::make_unique<juce::FileChooser> ("Export Theme", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                        .getChildFile (juce::File::createLegalFileName (name) + ".atptheme"),
                                                   "*.atptheme");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<ThemePanel> (this), name] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        auto file = fc.getResult().withFileExtension ("atptheme");
        auto* o = new juce::DynamicObject();
        o->setProperty ("app", "Audio Tech Pro");
        o->setProperty ("kind", "theme");
        o->setProperty ("version", 1);
        o->setProperty ("name", file.getFileNameWithoutExtension().isNotEmpty() ? file.getFileNameWithoutExtension() : name);
        const auto copy = safe->whole;   // (the main theme, with the editor's, toolbar's and MultiMeter's in it)
        if (copy.themePack.isNotEmpty())
            o->setProperty ("note", "Uses a REAPER theme (" + juce::File (copy.themePack).getFileName() + "): load it too for its pictures");
        o->setProperty ("settings", copy.toString());
        if (! file.replaceWithText (juce::JSON::toString (juce::var (o), false)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export Theme", "Couldn't write " + file.getFullPathName());
    });
}

void ThemePanel::importTheme()
{
    chooser = std::make_unique<juce::FileChooser> ("Import Theme", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.atptheme");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<ThemePanel> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || ! fc.getResult().existsAsFile())
            return;
        const auto json = juce::JSON::parse (fc.getResult().loadFileAsString());
        const auto text = json.getProperty ("settings", {}).toString();
        if (json.getProperty ("kind", {}).toString() != "theme" || text.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import Theme", "That isn't an Audio Tech Pro theme file.");
            return;
        }
        auto& self = *safe;
        self.whole = theme::Settings::fromString (text);   // (with its parts' own themes)
        self.scopeBox.setSelectedItemIndex (0, juce::dontSendNotification);
        self.editScope (0);
        if (self.onChange)
            self.onChange (self.whole);
        self.updateMeterButtons();
        self.repaint();
        if (self.store != nullptr)
        {
            auto name = json.getProperty ("name", {}).toString().trim();
            if (name.isEmpty())
                name = fc.getResult().getFileNameWithoutExtension();
            auto all = self.loadPresets();
            all.set (name, text);
            self.savePresets (all);
            self.refreshMyPresets();
            self.myPresets.setText (name, juce::dontSendNotification);
        }
        if (const auto note = json.getProperty ("note", {}).toString(); note.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import Theme", note);
    });
}

} // namespace riff
