#include "Theme.h"
#include "ThemePack.h"

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
        { &waveform, juce::Colour (0xffeef3f8u), juce::Colour (0xffeef3f8u), Role::fixed },
        { &locators, juce::Colour (0xffe8892au), juce::Colour (0xffe8892au), Role::fixed },
        { &markers, juce::Colour (0xff4b4e54u), juce::Colour (0xff4b4e54u), Role::fixed }
        };
        return entries;
    }

    Settings& settingsNow() { static Settings s; return s; }

    /** A skin's own colours: (colour, value) pairs; anything not listed takes the flat skin's. */
    using SkinTable = std::vector<std::pair<const juce::Colour*, juce::Colour>>;

    juce::Colour fromTable (const SkinTable& values, const juce::Colour* c, juce::Colour fallback)
    {
        for (const auto& [colour, value] : values)
            if (colour == c)
                return value;
        return fallback;
    }

    /** Pro Tools style: mid-dark greys, blue selection, green-to-red meters, dark waveforms in coloured clips. */
    juce::Colour protoolsValue (const juce::Colour* c, juce::Colour fallback)
    {
        using namespace juce;
        static const SkinTable values {
            { &background, Colour (0xff2d2e30) }, { &toolbar, Colour (0xff3c3d40) }, { &toolbarDark, Colour (0xff333437) },
            { &panel, Colour (0xff3a3b3e) }, { &laneA, Colour (0xff2a2b2d) }, { &laneB, Colour (0xff2e2f31) },
            { &outline, Colour (0xff505154) }, { &gridBar, Colour (0xff57585b) }, { &gridBeat, Colour (0xff38393c) },
            { &text, Colour (0xffe4e4e4) }, { &dimText, Colour (0xff9d9ea2) }, { &accent, Colour (0xff4a9fe8) },
            { &selection, Colour (0xff2f4a66) }, { &meterGreen, Colour (0xff3fcf4a) }, { &meterAmber, Colour (0xffe6d23a) },
            { &meterRed, Colour (0xffe8382f) },
            { &cream, Colour (0xff47484c) }, { &creamShade, Colour (0xff3e3f42) }, { &creamDeep, Colour (0xff2a2b2e) },
            { &ink, Colour (0xffe4e4e4) }, { &inkDim, Colour (0xff9d9ea2) }, { &gold, Colour (0xff4a9fe8) }, { &goldDark, Colour (0xff2f6fae) },
            { &ruler, Colour (0xff404144) }, { &rulerDark, Colour (0xff36373a) },
            { &console, Colour (0xff434448) }, { &consoleShade, Colour (0xff3a3b3f) }, { &consoleDark, Colour (0xff1f2022) },
            { &slotDark, Colour (0xff26272a) }, { &stripFace, Colour (0xff4a4b4f) }, { &stripFaceLow, Colour (0xff404145) },
            { &stripEdge, Colour (0xff222325) }, { &stripInk, Colour (0xffe4e4e4) }, { &stripInkDim, Colour (0xffa3a4a8) },
            { &stripSlot, Colour (0xff2f3033) }, { &stripSlotOn, Colour (0xff3a4a5c) }, { &knobArc, Colour (0xff4a9fe8) },
            { &lcd, Colour (0xff151618) }, { &lcdText, Colour (0xff7fd27b) }, { &lcdDim, Colour (0xff5d7a5b) },
            { &knobBlue, Colour (0xff4a9fe8) }, { &knobBlack, Colour (0xff4a9fe8) }, { &neon, Colour (0xff4a9fe8) },
            { &waveform, Colour (0xff141414) }, { &locators, Colour (0xff4a9fe8) }, { &markers, Colour (0xffd6c23c) },
        };
        return fromTable (values, c, fallback);
    }

    /** Ableton style: flat charcoal surfaces, orange accent and locators, bright clips with dark waveforms. */
    juce::Colour abletonValue (const juce::Colour* c, juce::Colour fallback)
    {
        using namespace juce;
        static const SkinTable values {
            { &background, Colour (0xff242424) }, { &toolbar, Colour (0xff363636) }, { &toolbarDark, Colour (0xff2c2c2c) },
            { &panel, Colour (0xff303030) }, { &laneA, Colour (0xff282828) }, { &laneB, Colour (0xff2b2b2b) },
            { &outline, Colour (0xff181818) }, { &gridBar, Colour (0xff4a4a4a) }, { &gridBeat, Colour (0xff353535) },
            { &text, Colour (0xffd8d8d8) }, { &dimText, Colour (0xff8e8e8e) }, { &accent, Colour (0xffffa236) },
            { &selection, Colour (0xff5c4a2a) }, { &meterGreen, Colour (0xff2fd17a) }, { &meterAmber, Colour (0xffffc23b) },
            { &meterRed, Colour (0xffff4a3a) },
            { &cream, Colour (0xff3a3a3a) }, { &creamShade, Colour (0xff323232) }, { &creamDeep, Colour (0xff222222) },
            { &ink, Colour (0xffd8d8d8) }, { &inkDim, Colour (0xff8e8e8e) }, { &gold, Colour (0xffffa236) }, { &goldDark, Colour (0xffb06c1c) },
            { &ruler, Colour (0xff2f2f2f) }, { &rulerDark, Colour (0xff272727) },
            { &console, Colour (0xff333333) }, { &consoleShade, Colour (0xff2c2c2c) }, { &consoleDark, Colour (0xff1b1b1b) },
            { &slotDark, Colour (0xff1e1e1e) }, { &stripFace, Colour (0xff363636) }, { &stripFaceLow, Colour (0xff303030) },
            { &stripEdge, Colour (0xff1a1a1a) }, { &stripInk, Colour (0xffd8d8d8) }, { &stripInkDim, Colour (0xff8e8e8e) },
            { &stripSlot, Colour (0xff262626) }, { &stripSlotOn, Colour (0xff4a3e2a) }, { &knobArc, Colour (0xffffa236) },
            { &lcd, Colour (0xff1c1c1c) }, { &lcdText, Colour (0xffffa236) }, { &lcdDim, Colour (0xff8a6a40) },
            { &knobBlue, Colour (0xffffa236) }, { &knobBlack, Colour (0xffffa236) }, { &neon, Colour (0xffffa236) },
            { &waveform, Colour (0xff1c1c1c) }, { &locators, Colour (0xffffa236) }, { &markers, Colour (0xff5a5a5a) },
        };
        return fromTable (values, c, fallback);
    }

    /** A REAPER theme's colours (its .ReaperTheme file), by what each of ours is for. */
    juce::Colour reaperValue (const juce::Colour* c, juce::Colour fallback)
    {
        using namespace juce;
        const auto get = [] (const char* key, Colour otherwise) { return themepack::colour (key).value_or (otherwise); };
        if (c == &background)  return get ("col_arrangebg", fallback);
        if (c == &laneA)       return get ("col_tr1_bg", fallback);
        if (c == &laneB)       return get ("col_tr2_bg", fallback);
        if (c == &toolbar || c == &panel || c == &cream) return get ("col_main_bg2", fallback);
        if (c == &toolbarDark || c == &creamShade) return get ("col_trans_bg", get ("col_main_bg2", fallback)).darker (0.08f);
        if (c == &creamDeep)   return get ("col_main_bg2", fallback).darker (0.35f);
        if (c == &outline)     return get ("col_tr1_divline", fallback).darker (0.2f);
        if (c == &gridBar)     return get ("col_tl_fg", fallback).withMultipliedAlpha (0.8f);
        if (c == &gridBeat)    return get ("arrange_vgrid", fallback);
        if (c == &text || c == &ink || c == &stripInk) return get ("col_main_text", fallback);
        if (c == &dimText || c == &inkDim || c == &stripInkDim) return get ("col_main_text2", fallback);
        if (c == &accent || c == &gold || c == &knobArc || c == &knobBlue || c == &knobBlack || c == &neon)
            return get ("col_toolbar_text_on", fallback);
        if (c == &goldDark)    return get ("col_toolbar_text_on", fallback).darker (0.4f);
        if (c == &selection)   return get ("col_seltrack", fallback);
        if (c == &meterGreen)  return get ("col_vubot", fallback);
        if (c == &meterAmber)  return get ("col_vumid", fallback);
        if (c == &meterRed)    return get ("col_vuclip", fallback);
        if (c == &ruler)       return get ("col_tl_bg", fallback);
        if (c == &rulerDark)   return get ("col_tl_bg", fallback).darker (0.2f);
        if (c == &console || c == &stripFace) return get ("col_mixerbg", fallback);
        if (c == &consoleShade || c == &stripFaceLow) return get ("col_mixerbg", fallback).darker (0.12f);
        if (c == &consoleDark || c == &stripEdge) return get ("col_mixerbg", fallback).darker (0.5f);
        if (c == &slotDark || c == &stripSlot) return get ("col_main_editbk", fallback).darker (0.25f);
        if (c == &stripSlotOn) return get ("col_main_3dhl", fallback).brighter (0.2f);
        if (c == &lcd)         return get ("col_trans_bg", fallback).darker (0.4f);
        if (c == &lcdText)     return get ("col_trans_fg", fallback).brighter (0.5f);
        if (c == &lcdDim)      return get ("col_trans_fg", fallback);
        if (c == &waveform)    return get ("col_tr1_peaks", fallback);
        if (c == &markers)     return get ("marker", fallback);
        if (c == &locators)    return get ("col_toolbar_text_on", fallback);
        return fallback;
    }

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

namespace
{
    Settings& editorNow() { static Settings s; return s; }
    bool& editorOwn() { static bool own = false; return own; }

    /** The colours the editor takes from its own theme (the tracks area, ruler, grid, regions, editors). */
    std::vector<juce::Colour*> editorColours()
    {
        return { &background, &laneA, &laneB, &gridBar, &gridBeat, &ruler, &rulerDark, &selection, &waveform,
                 &locators, &markers, &rollWhite, &rollBlack, &keysWhite, &keysBlack };
    }
}

void applyColours (const Settings& s);

namespace
{
    struct AreaData { bool own = false; Settings settings; std::vector<juce::Colour> colours; };
    AreaData& areaData (Area a) { static AreaData data[3]; return data[(int) a]; }
    int& areaDepth() { static int depth = 0; return depth; }

    std::vector<juce::Colour> colourValues()
    {
        std::vector<juce::Colour> v;
        for (auto* c : allColours())
            v.push_back (*c);
        return v;
    }
    void writeColours (const std::vector<juce::Colour>& v)
    {
        const auto targets = allColours();
        for (size_t i = 0; i < targets.size() && i < v.size(); ++i)
            *targets[i] = v[i];
    }
    void setMain (const Settings& s);
}

void set (const Settings& s)
{
    // The parts with themes of their own first (their colours kept aside), then the main theme.
    while (areaDepth() > 0)
        leaveArea();
    for (const auto& [area, text] : { std::pair { Area::toolbar, s.toolbarTheme }, std::pair { Area::meter, s.meterTheme } })
    {
        auto& a = areaData (area);
        a.own = text.isNotEmpty();
        if (! a.own)
            continue;
        auto own = Settings::fromString (text);
        own.editorTheme = own.toolbarTheme = own.meterTheme = {};
        applyColours (own);
        a.settings = own;
        a.colours = colourValues();
    }
    setMain (s);
    auto& main = areaData (Area::main);
    main.own = true;
    main.settings = settingsNow();
    main.colours = colourValues();
    for (auto area : { Area::toolbar, Area::meter })
        if (auto& a = areaData (area); ! a.own)
        {
            a.settings = main.settings;
            a.colours = main.colours;
        }
}

void enterArea (Area area)
{
    ++areaDepth();
    if (area != Area::main && areaData (area).own)
    {
        writeColours (areaData (area).colours);
        settingsNow() = areaData (area).settings;
    }
}

void leaveArea()
{
    if (areaDepth() <= 0)
        return;
    if (--areaDepth() == 0 && ! areaData (Area::main).colours.empty())
    {
        writeColours (areaData (Area::main).colours);
        settingsNow() = areaData (Area::main).settings;
    }
}

bool areaOwn (Area area) { return area != Area::main && areaData (area).own; }

std::vector<juce::Colour> areaColours (Area area)
{
    const auto& a = areaData (area);
    return a.colours.empty() ? colourValues() : a.colours;
}

namespace
{
void setMain (const Settings& s)
{
    // The editor's own theme first (its colours kept), then the main one; the editor's go back on top.
    editorOwn() = s.editorTheme.isNotEmpty();
    std::vector<juce::Colour> editorValues;
    if (editorOwn())
    {
        editorNow() = Settings::fromString (s.editorTheme);
        editorNow().editorTheme = {};
        applyColours (editorNow());
        for (auto* c : editorColours())
            editorValues.push_back (*c);
    }
    applyColours (s);
    if (editorOwn())
    {
        const auto targets = editorColours();
        for (size_t i = 0; i < targets.size(); ++i)
            *targets[i] = editorValues[i];
    }
}
}

const Settings& editorSettings() { return editorOwn() ? editorNow() : settingsNow(); }

void applyColours (const Settings& s)
{
    settingsNow() = s;
    themepack::use (s.skin == Skin::reaper && s.themePack.isNotEmpty() ? juce::File (s.themePack) : juce::File());
    const auto skin = s.skin;
    const auto pick = [skin] (const Entry& e)
    {
        switch (skin)
        {
            case Skin::white:    return whiteValue (e.colour, e.flat);
            case Skin::xenon:    return xenonValue (e.colour, e.flat);
            case Skin::protools: return protoolsValue (e.colour, e.flat);
            case Skin::ableton:  return abletonValue (e.colour, e.flat);
            case Skin::reaper:   return reaperValue (e.colour, e.flat);
            case Skin::flat:     return e.flat;
            case Skin::studio:   break;
        }
        return e.studio;
    };
    const auto base = pick (table()[0]);   // the background
    for (auto& e : table())
        *e.colour = adjust (pick (e), e.role, s, base.getBrightness());

    // Your own meter colours (still brightened or dimmed by Meters).
    for (auto [chosen, target] : { std::pair { s.meterLow, &meterGreen }, std::pair { s.meterMid, &meterAmber }, std::pair { s.meterPeak, &meterRed } })
        if (chosen != 0)
            *target = adjust (juce::Colour (chosen), Role::meter, s, base.getBrightness());
    // Your own text colours (the dim ones a step towards the background)
    if (s.textColour != 0)
    {
        text = ink = juce::Colour (s.textColour);
        dimText = inkDim = text.interpolatedWith (background, 0.38f);
    }
    if (s.stripTextColour != 0)
    {
        stripInk = juce::Colour (s.stripTextColour);
        stripInkDim = stripInk.interpolatedWith (stripFace, 0.38f);
    }
    if (s.locatorColour != 0) locators = juce::Colour (s.locatorColour);
    if (s.markerColour != 0)  markers = juce::Colour (s.markerColour);

    // The piano roll's rows: from the tracks' lanes, or a REAPER theme's MIDI editor colours.
    rollWhite = laneB;
    rollBlack = laneA.darker (0.25f);
    keysWhite = juce::Colour (0xffe8e8e8);
    keysBlack = juce::Colour (0xff1a1a1a);
    if (skin == Skin::reaper)
    {
        if (const auto c = themepack::colour ("midi_pkey1")) keysWhite = *c;
        if (const auto c = themepack::colour ("midi_pkey2")) keysBlack = *c;
        if (const auto c = themepack::colour ("midi_trackbg1")) rollWhite = *c;
        if (const auto c = themepack::colour ("midi_trackbg2")) rollBlack = *c;
        if (const auto c = themepack::colour ("midi_grid2"); c && c->getBrightness() > 0.05f) gridBar = c->withMultipliedAlpha (0.45f);
    }

    // Your own edit window colours.
    if (s.editBack != 0)
    {
        laneA = juce::Colour (s.editBack);
        laneB = laneA.getBrightness() > 0.5f ? laneA.darker (0.05f) : laneA.brighter (0.07f);
    }
    if (s.gridBarColour != 0)  gridBar = juce::Colour (s.gridBarColour);
    if (s.gridBeatColour != 0) gridBeat = juce::Colour (s.gridBeatColour);
    if (s.rollBack != 0)
    {
        rollWhite = juce::Colour (s.rollBack);
        rollBlack = rollWhite.getBrightness() > 0.5f ? rollWhite.darker (0.25f) : rollWhite.darker (0.45f);
    }
}

const Settings& current() { return settingsNow(); }

int trackColouring() { return settingsNow().trackColouring; }

juce::Colour trackFace (juce::Colour face, juce::Colour trackColour)
{
    switch (trackColouring())
    {
        case 1:  return face.interpolatedWith (trackColour, 0.32f);
        case 2:  return trackColour.interpolatedWith (face, 0.12f);
        default: return face;
    }
}

juce::Colour inkOn (juce::Colour face, juce::Colour normalInk)
{
    // The usual ink if it reads; else black or white, whichever reads better
    const auto contrast = std::abs (face.getPerceivedBrightness() - normalInk.getPerceivedBrightness());
    if (contrast > 0.35f)
        return normalInk;
    return face.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff101214) : juce::Colour (0xfff2f3f5);
}

int gridStyle() { return editorSettings().gridStyle; }

void drawGridLine (juce::Graphics& g, float x, float top, float height, bool isBar, bool isBeat, juce::Colour colour)
{
    switch (gridStyle())
    {
        case 5: return;                                       // none
        case 2: if (! isBar) return; break;                   // bars only
        case 3:                                               // dotted: bars solid, the rest dots
            if (! isBar)
            {
                g.setColour (colour.withMultipliedAlpha (isBeat ? 1.0f : 0.8f));
                for (float y = top; y < top + height; y += 4.0f)
                    g.fillRect (x, y, 1.0f, juce::jmin (1.5f, top + height - y));
                return;
            }
            break;
        default: break;
    }
    g.setColour (colour);
    g.fillRect (x, top, 1.0f, height);
}

void shadeGridSpan (juce::Graphics& g, float x0, float x1, float top, float height)
{
    g.setColour (juce::Colours::black.withAlpha (0.17f));
    g.fillRect (x0, top, x1 - x0, height);
}
bool imageSkin() { return settingsNow().skin == Skin::studio; }
bool whiteSkin() { return settingsNow().skin == Skin::white; }
bool xenonSkin() { return settingsNow().skin == Skin::xenon; }
bool reaperSkin() { return settingsNow().skin == Skin::reaper && themepack::active(); }

int knobStyle()   { return settingsNow().knobStyle; }
int buttonStyle() { return settingsNow().buttonStyle; }
int meterStyle()  { return settingsNow().meterStyle; }

int faderStyle()
{
    const auto& s = settingsNow();
    if (s.faderStyle >= 0)
        return s.faderStyle == 3 && ! themepack::active() ? 0 : s.faderStyle;
    switch (s.skin)
    {
        case Skin::white:  return 1;
        case Skin::xenon:  return 2;
        case Skin::reaper: return themepack::active() ? 3 : 0;
        default:           return 0;
    }
}

int waveformMode()
{
    const auto& s = editorSettings();
    return s.waveMode >= 0 ? s.waveMode : (s.skin == Skin::xenon ? 2 : (s.skin == Skin::protools || s.skin == Skin::ableton) ? 1 : 0);
}
juce::Colour waveformA()
{
    const auto& s = editorSettings();
    return s.waveA != 0 ? juce::Colour (s.waveA) : (s.skin == Skin::xenon ? juce::Colour (0xff7ae0de) : waveform);   // (Pro Tools / Ableton: dark)
}
juce::Colour waveformB()
{
    const auto& s = editorSettings();
    return s.waveB != 0 ? juce::Colour (s.waveB) : (s.skin == Skin::xenon ? juce::Colour (0xffa77fe0) : waveform);
}
bool glassRegions()
{
    const auto& s = editorSettings();
    return s.glassRegions >= 0 ? s.glassRegions == 1 : s.skin == Skin::xenon;
}

std::vector<juce::Colour*> allColours()
{
    std::vector<juce::Colour*> result;
    for (auto& e : table())
        result.push_back (e.colour);
    result.push_back (&rollWhite);
    result.push_back (&rollBlack);
    result.push_back (&keysWhite);
    result.push_back (&keysBlack);
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
                               juce::String (glassRegions),
                               juce::String::toHexString ((int) locatorColour), juce::String::toHexString ((int) markerColour),
                               juce::URL::addEscapeChars (themePack, false), juce::String (faderStyle),
                               juce::String::toHexString ((int) editBack), juce::String::toHexString ((int) gridBarColour),
                               juce::String::toHexString ((int) gridBeatColour), juce::String::toHexString ((int) rollBack),
                               juce::String (knobStyle), juce::String (buttonStyle), juce::String (meterStyle),
                               juce::URL::addEscapeChars (editorTheme, false),
                               juce::URL::addEscapeChars (toolbarTheme, false), juce::URL::addEscapeChars (meterTheme, false),
                               juce::String::toHexString ((int) textColour), juce::String::toHexString ((int) stripTextColour),
                               juce::String (trackColouring), juce::String (gridStyle) }.joinIntoString (";");
}

Settings Settings::fromString (const juce::String& text)
{
    Settings s;
    const auto v = juce::StringArray::fromTokens (text, ";", "");
    if (v.size() < 9)
        return s;
    s.skin = (Skin) juce::jlimit (0, (int) Skin::reaper, v[0].getIntValue());
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
    if (v.size() >= 18)
    {
        s.locatorColour = (juce::uint32) v[16].getHexValue32();
        s.markerColour = (juce::uint32) v[17].getHexValue32();
    }
    if (v.size() >= 20)
    {
        s.themePack = juce::URL::removeEscapeChars (v[18]);
        s.faderStyle = juce::jlimit (-1, 7, v[19].getIntValue());
    }
    if (v.size() >= 24)
    {
        s.editBack = (juce::uint32) v[20].getHexValue32();
        s.gridBarColour = (juce::uint32) v[21].getHexValue32();
        s.gridBeatColour = (juce::uint32) v[22].getHexValue32();
        s.rollBack = (juce::uint32) v[23].getHexValue32();
    }
    if (v.size() >= 27)
    {
        s.knobStyle = juce::jlimit (-1, 6, v[24].getIntValue());
        s.buttonStyle = juce::jlimit (-1, 3, v[25].getIntValue());
        s.meterStyle = juce::jlimit (-1, 2, v[26].getIntValue());
    }
    if (v.size() >= 28)
        s.editorTheme = juce::URL::removeEscapeChars (v[27]);
    if (v.size() >= 30)
    {
        s.toolbarTheme = juce::URL::removeEscapeChars (v[28]);
        s.meterTheme = juce::URL::removeEscapeChars (v[29]);
    }
    if (v.size() >= 34)
    {
        s.textColour = (juce::uint32) v[30].getHexValue32();
        s.stripTextColour = (juce::uint32) v[31].getHexValue32();
        s.trackColouring = juce::jlimit (0, 2, v[32].getIntValue());
        s.gridStyle = juce::jlimit (0, 5, v[33].getIntValue());
    }
    return s;
}

} // namespace riff::theme
