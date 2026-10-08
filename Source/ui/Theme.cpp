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

} // namespace riff::theme
