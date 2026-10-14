#include "Theme.h"

namespace riff::theme
{

void apply (juce::LookAndFeel& lf)
{
    using namespace juce;

    lf.setColour (ResizableWindow::backgroundColourId,     background);
    lf.setColour (DocumentWindow::backgroundColourId,      background);
    lf.setColour (TextButton::buttonColourId,              panel.brighter (0.12f));
    lf.setColour (TextButton::buttonOnColourId,            accent);
    lf.setColour (TextButton::textColourOffId,             text);
    lf.setColour (TextButton::textColourOnId,              Colours::black);
    lf.setColour (ComboBox::outlineColourId,               outline);
    lf.setColour (ComboBox::backgroundColourId,            panel);
    lf.setColour (ComboBox::textColourId,                  text);
    lf.setColour (ComboBox::arrowColourId,                 dimText);
    lf.setColour (Slider::trackColourId,                   accent.withAlpha (0.8f));
    lf.setColour (Slider::thumbColourId,                   text);
    lf.setColour (Slider::backgroundColourId,              panel.brighter (0.15f));
    lf.setColour (Slider::rotarySliderFillColourId,        accent);
    lf.setColour (Slider::rotarySliderOutlineColourId,     panel.brighter (0.2f));
    lf.setColour (Slider::textBoxTextColourId,             text);
    lf.setColour (Slider::textBoxOutlineColourId,          outline);
    lf.setColour (Label::textColourId,                     text);
    lf.setColour (Label::textWhenEditingColourId,          text);
    lf.setColour (Label::backgroundWhenEditingColourId,    lcd);
    lf.setColour (Label::outlineWhenEditingColourId,       accent);
    lf.setColour (TextEditor::backgroundColourId,          lcd);
    lf.setColour (TextEditor::textColourId,                text);
    lf.setColour (TextEditor::highlightColourId,           accent.withAlpha (0.4f));
    lf.setColour (TextEditor::outlineColourId,             outline);
    lf.setColour (CaretComponent::caretColourId,           text);
    lf.setColour (PopupMenu::backgroundColourId,           panel);
    lf.setColour (PopupMenu::textColourId,                 text);
    lf.setColour (PopupMenu::headerTextColourId,           gold);
    lf.setColour (PopupMenu::highlightedBackgroundColourId, accent);
    lf.setColour (PopupMenu::highlightedTextColourId,      Colours::white);
    lf.setColour (ScrollBar::thumbColourId,                outline.brighter (0.25f));
    lf.setColour (AlertWindow::backgroundColourId,         panel);
    lf.setColour (AlertWindow::textColourId,               text);
    lf.setColour (AlertWindow::outlineColourId,            outline);
    lf.setColour (TooltipWindow::backgroundColourId,       toolbarDark);
    lf.setColour (TooltipWindow::textColourId,             text);
    lf.setColour (TooltipWindow::outlineColourId,          outline);
    lf.setColour (ListBox::backgroundColourId,             background);
    lf.setColour (ListBox::textColourId,                   text);
    lf.setColour (TreeView::backgroundColourId,            background);
    lf.setColour (TreeView::linesColourId,                 outline);
    lf.setColour (TreeView::selectedItemBackgroundColourId, selection);
    lf.setColour (TableHeaderComponent::backgroundColourId, panel);
    lf.setColour (TableHeaderComponent::textColourId,      text);
    lf.setColour (TableHeaderComponent::outlineColourId,   outline);
    lf.setColour (TableListBox::backgroundColourId,        background);
    lf.setColour (ToggleButton::textColourId,              text);
    lf.setColour (ToggleButton::tickColourId,              accent);
    lf.setColour (ProgressBar::backgroundColourId,         lcd);
    lf.setColour (ProgressBar::foregroundColourId,         accent);
    lf.setColour (DirectoryContentsDisplayComponent::highlightColourId, accent);
    lf.setColour (KeyMappingEditorComponent::backgroundColourId, background);
    lf.setColour (KeyMappingEditorComponent::textColourId, text);
}


//==============================================================================
namespace
{
    enum class Role { surface, text, accent, meter, grid, fixed };
    struct Entry { juce::Colour* colour; juce::Colour studio, flat; Role role; };

    std::vector<Entry>& table()
    {
        static std::vector<Entry> entries {
        { &background, juce::Colour (0xff1e2829u), juce::Colour (0xff2b3237u), Role::surface },
        { &toolbar, juce::Colour (0xff2a3334u), juce::Colour (0xff3a434au), Role::surface },
        { &toolbarDark, juce::Colour (0xff1f2829u), juce::Colour (0xff2f373du), Role::surface },
        { &panel, juce::Colour (0xff2c3637u), juce::Colour (0xff384148u), Role::surface },
        { &laneA, juce::Colour (0xff212b2cu), juce::Colour (0xff2c3338u), Role::surface },
        { &laneB, juce::Colour (0xff252f30u), juce::Colour (0xff30383du), Role::surface },
        { &outline, juce::Colour (0xff3d494au), juce::Colour (0xff4a545bu), Role::surface },
        { &gridBar, juce::Colour (0xff465253u), juce::Colour (0xff515c63u), Role::grid },
        { &gridBeat, juce::Colour (0xff2e3839u), juce::Colour (0xff394247u), Role::grid },
        { &text, juce::Colour (0xffe8e9ecu), juce::Colour (0xffdfe6eau), Role::text },
        { &dimText, juce::Colour (0xff9a9ca3u), juce::Colour (0xff95a1a8u), Role::text },
        { &accent, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent },
        { &selection, juce::Colour (0xff26364du), juce::Colour (0xff4a3a24u), Role::accent },
        { &mute, juce::Colour (0xffe2463fu), juce::Colour (0xffe2463fu), Role::fixed },
        { &solo, juce::Colour (0xfff2c94eu), juce::Colour (0xfff2c94eu), Role::fixed },
        { &danger, juce::Colour (0xffe2463fu), juce::Colour (0xffe2463fu), Role::fixed },
        { &meterGreen, juce::Colour (0xff45c25au), juce::Colour (0xff5ccf4au), Role::meter },
        { &meterAmber, juce::Colour (0xffe8c53cu), juce::Colour (0xffffc23au), Role::meter },
        { &meterRed, juce::Colour (0xffe2463fu), juce::Colour (0xffff4a3au), Role::meter },
        { &cream, juce::Colour (0xff3b3d41u), juce::Colour (0xff434c53u), Role::surface },
        { &creamShade, juce::Colour (0xff313336u), juce::Colour (0xff3a4248u), Role::surface },
        { &creamDeep, juce::Colour (0xff232427u), juce::Colour (0xff2b3237u), Role::surface },
        { &ink, juce::Colour (0xffe8e9ecu), juce::Colour (0xffdfe6eau), Role::text },
        { &inkDim, juce::Colour (0xff9a9ca3u), juce::Colour (0xff95a1a8u), Role::text },
        { &gold, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent },
        { &goldDark, juce::Colour (0xff1d5fbfu), juce::Colour (0xffc9701au), Role::accent },
        { &ruler, juce::Colour (0xff364042u), juce::Colour (0xff3d464cu), Role::surface },
        { &rulerDark, juce::Colour (0xff283132u), juce::Colour (0xff31393eu), Role::surface },
        { &console, juce::Colour (0xff2f3134u), juce::Colour (0xff353d43u), Role::surface },
        { &consoleShade, juce::Colour (0xff27282bu), juce::Colour (0xff2f363bu), Role::surface },
        { &consoleDark, juce::Colour (0xff141516u), juce::Colour (0xff1f2427u), Role::surface },
        { &slotDark, juce::Colour (0xff18191bu), juce::Colour (0xff262c30u), Role::surface },
        { &stripFace, juce::Colour (0xff959c9cu), juce::Colour (0xff3b444au), Role::surface },
        { &stripFaceLow, juce::Colour (0xff818888u), juce::Colour (0xff343c41u), Role::surface },
        { &stripEdge, juce::Colour (0xff4e5556u), juce::Colour (0xff22282bu), Role::surface },
        { &stripInk, juce::Colour (0xff101214u), juce::Colour (0xffdfe6eau), Role::text },
        { &stripInkDim, juce::Colour (0xff3c4044u), juce::Colour (0xff9aa6adu), Role::text },
        { &stripSlot, juce::Colour (0xff2b3436u), juce::Colour (0xff2a3135u), Role::surface },
        { &stripSlotOn, juce::Colour (0xff253032u), juce::Colour (0xff232a2eu), Role::surface },
        { &knobArc, juce::Colour (0xff4a7aa6u), juce::Colour (0xffff9a30u), Role::accent },
        { &lcd, juce::Colour (0xff0b0c0eu), juce::Colour (0xff1c2124u), Role::surface },
        { &lcdText, juce::Colour (0xffdfe6f2u), juce::Colour (0xffc8ff9au), Role::text },
        { &lcdDim, juce::Colour (0xff7c8392u), juce::Colour (0xff7b8a83u), Role::text },
        { &knobCream, juce::Colour (0xffd9ecffu), juce::Colour (0xffd9e2e6u), Role::fixed },
        { &knobBlue, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent },
        { &knobRed, juce::Colour (0xffff5a4au), juce::Colour (0xffff5a4au), Role::fixed },
        { &knobBlack, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent },
        { &neon, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent },
        { &waveform, juce::Colour (0xffeef3f8u), juce::Colour (0xffeef3f8u), Role::fixed }
        };
        return entries;
    }

    Settings& settingsNow() { static Settings s; return s; }

    /** Xenon's own colours (sampled from your picture). */
    juce::Colour xenonValue (const juce::Colour* c, juce::Colour fallback)
    {
        using namespace juce;
        static const std::vector<std::pair<const Colour*, Colour>> values {
            { &background, Colour (0xff13111c) }, { &toolbar, Colour (0xff221f2b) }, { &toolbarDark, Colour (0xff1a1822) },
            { &panel, Colour (0xff1d1b27) }, { &laneA, Colour (0xff15131e) }, { &laneB, Colour (0xff181622) },
            { &outline, Colour (0xff343346) }, { &gridBar, Colour (0xff3a3858) }, { &gridBeat, Colour (0xff23213a) },
            { &text, Colour (0xffd9e6ef) }, { &dimText, Colour (0xff8a92a6) }, { &accent, Colour (0xff6fe2d2) },
            { &selection, Colour (0xff27384a) }, { &meterGreen, Colour (0xff6fd8c8) }, { &meterAmber, Colour (0xffc9c25a) },
            { &meterRed, Colour (0xffe2563f) },
            { &cream, Colour (0xff23212d) }, { &creamShade, Colour (0xff1c1a25) }, { &creamDeep, Colour (0xff121018) },
            { &ink, Colour (0xffd9e6ef) }, { &inkDim, Colour (0xff8a92a6) }, { &gold, Colour (0xff6fe2d2) },
            { &goldDark, Colour (0xff3a9c90) }, { &ruler, Colour (0xff1c1a26) }, { &rulerDark, Colour (0xff15131d) },
            { &console, Colour (0xff12161c) }, { &consoleShade, Colour (0xff0e1217) }, { &consoleDark, Colour (0xff0a0d11) },
            { &slotDark, Colour (0xff10151a) }, { &stripFace, Colour (0xff111820) }, { &stripFaceLow, Colour (0xff0c1217) },
            { &stripEdge, Colour (0xff050709) }, { &stripInk, Colour (0xffd2dde6) }, { &stripInkDim, Colour (0xff7d8794) },
            { &stripSlot, Colour (0xff121a20) }, { &stripSlotOn, Colour (0xff1d4a44) }, { &knobArc, Colour (0xff6fe2d2) },
            { &lcd, Colour (0xff1a2532) }, { &lcdText, Colour (0xff8fd6f4) }, { &lcdDim, Colour (0xff5f7a8e) },
            { &knobBlue, Colour (0xff6fe2d2) }, { &knobBlack, Colour (0xff6fe2d2) }, { &neon, Colour (0xff6fe2d2) },
            { &waveform, Colour (0xff7ae0de) },
        };
        for (const auto& [colour, value] : values)
            if (colour == c)
                return value;
        return fallback;
    }

    /** Studio White's own colours (the light console in your pictures); the rest follow the flat skin. */
    juce::Colour whiteValue (const juce::Colour* c, juce::Colour fallback)
    {
        using namespace juce;
        static const std::vector<std::pair<const Colour*, Colour>> values {
            { &background, Colour (0xffe6e4de) }, { &toolbar, Colour (0xfff2f0eb) }, { &toolbarDark, Colour (0xffe4e2dc) },
            { &panel, Colour (0xffeeece7) }, { &laneA, Colour (0xfff4f3ef) }, { &laneB, Colour (0xffecebe6) },
            { &outline, Colour (0xffc6c3bb) }, { &gridBar, Colour (0xffc0bdb5) }, { &gridBeat, Colour (0xffdcd9d2) },
            { &text, Colour (0xff26282c) }, { &dimText, Colour (0xff767980) }, { &accent, Colour (0xff3b8fe2) },
            { &selection, Colour (0xffcde0f6) }, { &meterGreen, Colour (0xff4cc35b) }, { &meterAmber, Colour (0xffe8b93a) },
            { &cream, Colour (0xffe2dfd8) }, { &creamShade, Colour (0xffd7d4cc) }, { &creamDeep, Colour (0xffc8c4bb) },
            { &ink, Colour (0xff26282c) }, { &inkDim, Colour (0xff6c6f75) }, { &gold, Colour (0xff3b8fe2) },
            { &goldDark, Colour (0xff2a6db3) }, { &ruler, Colour (0xffebe8e1) }, { &rulerDark, Colour (0xffdedbd3) },
            { &console, Colour (0xffe9e7e1) }, { &consoleShade, Colour (0xffdfdcd5) }, { &consoleDark, Colour (0xffc5c2ba) },
            { &slotDark, Colour (0xffdedbd4) }, { &stripFace, Colour (0xfff1efea) }, { &stripFaceLow, Colour (0xffe3e0d9) },
            { &stripEdge, Colour (0xffbcb8af) }, { &stripInk, Colour (0xff26282c) }, { &stripInkDim, Colour (0xff6c6f75) },
            { &stripSlot, Colour (0xffdcd9d1) }, { &stripSlotOn, Colour (0xffcfe0f3) }, { &knobArc, Colour (0xff3b8fe2) },
            { &lcd, Colour (0xff1b2124) }, { &lcdText, Colour (0xffdfe6f2) }, { &lcdDim, Colour (0xff7c8392) },
            { &knobBlue, Colour (0xff3b8fe2) }, { &knobBlack, Colour (0xff3b8fe2) }, { &neon, Colour (0xff3b8fe2) },
        };
        for (const auto& [colour, value] : values)
            if (colour == c)
                return value;
        return fallback;
    }

    juce::Colour adjust (juce::Colour c, Role role, const Settings& s, float backgroundBrightness)
    {
        auto h = c.getHue(), sat = c.getSaturation(), b = c.getBrightness();
        switch (role)
        {
            case Role::fixed:
                return c;
            case Role::meter:
                return c.withBrightness (juce::jlimit (0.0f, 1.0f, b * s.meterBrightness));
            case Role::accent:
                if (s.accentHue >= 0.0f)
                    h = s.accentHue / 360.0f;
                return juce::Colour::fromHSV (h, sat, b, c.getFloatAlpha());
            case Role::text:
                if (b > 0.5f)
                    b = s.textBrightness >= 0.0f ? b + s.textBrightness * (1.0f - b) : b * (1.0f + 0.6f * s.textBrightness);
                else
                    b = s.textBrightness >= 0.0f ? b * (1.0f - s.textBrightness) : b + (0.5f - b) * -s.textBrightness;
                return juce::Colour::fromHSV (h, sat, juce::jlimit (0.0f, 1.0f, b), c.getFloatAlpha());
            case Role::grid:
            case Role::surface:
                break;
        }

        // Surfaces (and the grid): tint, saturation, lightness, contrast.
        h = std::fmod (h + s.hue / 360.0f + 1.0f, 1.0f);
        sat = juce::jlimit (0.0f, 1.0f, (sat < 0.02f && std::abs (s.hue) > 0.5f ? 0.08f : sat) * s.saturation);
        b = backgroundBrightness + (b - backgroundBrightness) * (1.0f + s.contrast);
        b *= 1.0f + s.lightness;
        if (role == Role::grid)
            b = backgroundBrightness * (1.0f + s.lightness) + (b - backgroundBrightness * (1.0f + s.lightness)) * s.gridBrightness;
        return juce::Colour::fromHSV (h, sat, juce::jlimit (0.0f, 1.0f, b), c.getFloatAlpha());
    }
}

void set (const Settings& s)
{
    settingsNow() = s;
    const bool flat = s.skin == Skin::flat, white = s.skin == Skin::white, xenon = s.skin == Skin::xenon;
    const auto pick = [flat, white, xenon] (const Entry& e)
    {
        return white ? whiteValue (e.colour, e.flat) : xenon ? xenonValue (e.colour, e.flat) : flat ? e.flat : e.studio;
    };
    const auto base = pick (table()[0]);   // the background
    for (auto& e : table())
        *e.colour = adjust (pick (e), e.role, s, base.getBrightness());

    // Your own meter colours (still brightened or dimmed by Meters).
    for (auto [chosen, target] : { std::pair { s.meterLow, &meterGreen }, std::pair { s.meterMid, &meterAmber }, std::pair { s.meterPeak, &meterRed } })
        if (chosen != 0)
            *target = adjust (juce::Colour (chosen), Role::meter, s, base.getBrightness());
}

const Settings& current() { return settingsNow(); }
bool imageSkin() { return settingsNow().skin == Skin::studio; }
bool whiteSkin() { return settingsNow().skin == Skin::white; }
bool xenonSkin() { return settingsNow().skin == Skin::xenon; }

int waveformMode()
{
    const auto& s = settingsNow();
    return s.waveMode >= 0 ? s.waveMode : (s.skin == Skin::xenon ? 2 : 0);
}
juce::Colour waveformA()
{
    const auto& s = settingsNow();
    return s.waveA != 0 ? juce::Colour (s.waveA) : (s.skin == Skin::xenon ? juce::Colour (0xff7ae0de) : waveform);
}
juce::Colour waveformB()
{
    const auto& s = settingsNow();
    return s.waveB != 0 ? juce::Colour (s.waveB) : (s.skin == Skin::xenon ? juce::Colour (0xffa77fe0) : waveform);
}
bool glassRegions()
{
    const auto& s = settingsNow();
    return s.glassRegions >= 0 ? s.glassRegions == 1 : s.skin == Skin::xenon;
}

std::vector<juce::Colour*> allColours()
{
    std::vector<juce::Colour*> result;
    for (auto& e : table())
        result.push_back (e.colour);
    return result;
}

juce::String Settings::toString() const
{
    return juce::StringArray { juce::String ((int) skin), juce::String (hue), juce::String (saturation), juce::String (lightness),
                               juce::String (contrast), juce::String (textBrightness), juce::String (accentHue),
                               juce::String (meterBrightness), juce::String (gridBrightness),
                               juce::String::toHexString ((int) meterLow), juce::String::toHexString ((int) meterMid),
                               juce::String::toHexString ((int) meterPeak),
                               juce::String (waveMode), juce::String::toHexString ((int) waveA), juce::String::toHexString ((int) waveB),
                               juce::String (glassRegions) }.joinIntoString (";");
}

Settings Settings::fromString (const juce::String& text)
{
    Settings s;
    const auto v = juce::StringArray::fromTokens (text, ";", "");
    if (v.size() < 9)
        return s;
    s.skin = v[0].getIntValue() == 3 ? Skin::xenon : v[0].getIntValue() == 2 ? Skin::white : v[0].getIntValue() == 1 ? Skin::flat : Skin::studio;
    s.hue = juce::jlimit (-180.0f, 180.0f, v[1].getFloatValue());
    s.saturation = juce::jlimit (0.0f, 2.0f, v[2].getFloatValue());
    s.lightness = juce::jlimit (-1.0f, 1.0f, v[3].getFloatValue());
    s.contrast = juce::jlimit (-1.0f, 1.0f, v[4].getFloatValue());
    s.textBrightness = juce::jlimit (-1.0f, 1.0f, v[5].getFloatValue());
    s.accentHue = juce::jlimit (-1.0f, 360.0f, v[6].getFloatValue());
    s.meterBrightness = juce::jlimit (0.3f, 1.6f, v[7].getFloatValue());
    s.gridBrightness = juce::jlimit (0.0f, 2.0f, v[8].getFloatValue());
    if (v.size() >= 12)
    {
        s.meterLow = (juce::uint32) v[9].getHexValue32();
        s.meterMid = (juce::uint32) v[10].getHexValue32();
        s.meterPeak = (juce::uint32) v[11].getHexValue32();
    }
    if (v.size() >= 16)
    {
        s.waveMode = juce::jlimit (-1, 2, v[12].getIntValue());
        s.waveA = (juce::uint32) v[13].getHexValue32();
        s.waveB = (juce::uint32) v[14].getHexValue32();
        s.glassRegions = juce::jlimit (-1, 1, v[15].getIntValue());
    }
    return s;
}

} // namespace riff::theme
