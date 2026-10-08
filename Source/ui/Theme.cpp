#include "Theme.h"

namespace riff::theme
{

void apply (juce::LookAndFeel& lf)
{
    using namespace juce;

    lf.setColour (ResizableWindow::backgroundColourId,     background);
    lf.setColour (TextButton::buttonColourId,              panel);
    lf.setColour (TextButton::buttonOnColourId,            accent);
    lf.setColour (TextButton::textColourOffId,             text);
    lf.setColour (TextButton::textColourOnId,              Colours::black);
    lf.setColour (ComboBox::outlineColourId,               outline);
    lf.setColour (ComboBox::backgroundColourId,            panel);
    lf.setColour (Slider::trackColourId,                   accent.withAlpha (0.8f));
    lf.setColour (Slider::thumbColourId,                   text);
    lf.setColour (Slider::backgroundColourId,              panel.brighter (0.15f));
    lf.setColour (Slider::rotarySliderFillColourId,        accent);
    lf.setColour (Slider::rotarySliderOutlineColourId,     panel.brighter (0.2f));
    lf.setColour (Label::textColourId,                     text);
    lf.setColour (Label::textWhenEditingColourId,          text);
    lf.setColour (Label::backgroundWhenEditingColourId,    panel.darker (0.3f));
    lf.setColour (Label::outlineWhenEditingColourId,       accent);
    lf.setColour (TextEditor::backgroundColourId,          panel.darker (0.3f));
    lf.setColour (TextEditor::textColourId,                text);
    lf.setColour (TextEditor::highlightColourId,           accent.withAlpha (0.4f));
    lf.setColour (PopupMenu::backgroundColourId,           panel);
    lf.setColour (PopupMenu::textColourId,                 text);
    lf.setColour (PopupMenu::highlightedBackgroundColourId, accent);
    lf.setColour (ScrollBar::thumbColourId,                outline.brighter (0.2f));
    lf.setColour (AlertWindow::backgroundColourId,         panel);
    lf.setColour (AlertWindow::textColourId,               text);
}

} // namespace riff::theme
