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
        { &neon, juce::Colour (0xff2f8cffu), juce::Colour (0xffff9a30u), Role::accent }
        };
        return entries;
    }

    Settings& settingsNow() { static Settings s; return s; }

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
    const bool flat = s.skin == Skin::flat;
    const auto base = flat ? table()[0].flat : table()[0].studio;   // the background
    for (auto& e : table())
        *e.colour = adjust (flat ? e.flat : e.studio, e.role, s, base.getBrightness());
}

const Settings& current() { return settingsNow(); }
bool imageSkin() { return settingsNow().skin == Skin::studio; }

juce::String Settings::toString() const
{
    return juce::StringArray { juce::String ((int) skin), juce::String (hue), juce::String (saturation), juce::String (lightness),
                               juce::String (contrast), juce::String (textBrightness), juce::String (accentHue),
                               juce::String (meterBrightness), juce::String (gridBrightness) }.joinIntoString (";");
}

Settings Settings::fromString (const juce::String& text)
{
    Settings s;
    const auto v = juce::StringArray::fromTokens (text, ";", "");
    if (v.size() < 9)
        return s;
    s.skin = v[0].getIntValue() == 1 ? Skin::flat : Skin::studio;
    s.hue = juce::jlimit (-180.0f, 180.0f, v[1].getFloatValue());
    s.saturation = juce::jlimit (0.0f, 2.0f, v[2].getFloatValue());
    s.lightness = juce::jlimit (-1.0f, 1.0f, v[3].getFloatValue());
    s.contrast = juce::jlimit (-1.0f, 1.0f, v[4].getFloatValue());
    s.textBrightness = juce::jlimit (-1.0f, 1.0f, v[5].getFloatValue());
    s.accentHue = juce::jlimit (-1.0f, 360.0f, v[6].getFloatValue());
    s.meterBrightness = juce::jlimit (0.3f, 1.6f, v[7].getFloatValue());
    s.gridBrightness = juce::jlimit (0.0f, 2.0f, v[8].getFloatValue());
    return s;
}

} // namespace riff::theme
