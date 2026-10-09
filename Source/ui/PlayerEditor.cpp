#include "PlayerEditor.h"
#include "Theme.h"
#include "../model/Generator.h"
#include "../model/Project.h"

namespace riff
{

namespace pl = generator::player;

PlayerEditor::PlayerEditor (juce::UndoManager& um) : undoManager (um)
{
    for (const auto& g : generator::genres())
        genreBox.addItem (g.name, genreBox.getNumItems() + 1);
    genreBox.onChange = [this] { set (pl::genre, genreBox.getSelectedId() - 1, true); };
    addAndMakeVisible (genreBox);

    for (auto [slider, label, name, id] : { std::tuple { &fills, &fillsLabel, "Fills", &pl::fills }, std::tuple { &swing, &swingLabel, "Swing", &pl::swing } })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider->setRange (0.0, 1.0, 0.01);
        slider->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        slider->updateText();
        slider->onDragStart = [this, name = juce::String (name)] { undoManager.beginNewTransaction ("Player " + name); };
        slider->onValueChange = [this, slider, id = *id] { set (id, slider->getValue(), false); };
        addAndMakeVisible (slider);
        label->setText (name, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    for (const auto* name : { "Kick & Snare", "Hi-Hats", "Cymbals", "Toms", "Percussion" })
    {
        auto* box = pieces.add (new juce::ToggleButton (name));
        box->setColour (juce::ToggleButton::textColourId, theme::text);
        const auto bit = pieces.size() - 1;
        box->onClick = [this, bit, box]
        {
            auto mask = (int) region.getProperty (pl::pieces, 0x1f);
            mask = box->getToggleState() ? (mask | (1 << bit)) : (mask & ~(1 << bit));
            set (pl::pieces, mask, true);
        };
        addAndMakeVisible (box);
    }

    newTake.setTooltip ("The same settings, played a new way");
    newTake.onClick = [this] { set (pl::seed, juce::Random::getSystemRandom().nextInt (100000) + 1, true); };
    addAndMakeVisible (newTake);
}

PlayerEditor::~PlayerEditor()
{
    region.removeListener (this);
}

void PlayerEditor::setRegion (juce::ValueTree r)
{
    region.removeListener (this);
    region = r;
    region.addListener (this);
    refresh();
    resized();
    repaint();
}

void PlayerEditor::refresh()
{
    if (! region.isValid())
        return;
    genreBox.setSelectedId ((int) region.getProperty (pl::genre, 0) + 1, juce::dontSendNotification);
    fills.setValue (region.getProperty (pl::fills, 0.4), juce::dontSendNotification);
    swing.setValue (region.getProperty (pl::swing, 0.0), juce::dontSendNotification);
    const bool drums = region[pl::kind].toString() == "drums";
    const int mask = region.getProperty (pl::pieces, 0x1f);
    for (int i = 0; i < pieces.size(); ++i)
    {
        pieces[i]->setVisible (drums);
        pieces[i]->setToggleState (((mask >> i) & 1) != 0, juce::dontSendNotification);
    }
}

void PlayerEditor::set (const juce::Identifier& id, const juce::var& value, bool newStep)
{
    if (! region.isValid())
        return;
    if (newStep)
        undoManager.beginNewTransaction ("Player");
    region.setProperty (id, value, &undoManager);
    pl::regenerate (region, &undoManager);
}

juce::Rectangle<int> PlayerEditor::padArea() const
{
    auto r = getLocalBounds().reduced (16);
    return r.removeFromLeft (juce::jmin (r.getHeight() + 40, r.getWidth() / 2));
}

void PlayerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff232427));
    if (! region.isValid())
        return;

    // The XY pad: left simple, right complex; bottom soft, top loud.
    const auto pad = padArea().toFloat();
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (pad, 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (pad, 6.0f, 1.0f);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("Simple", pad.reduced (8.0f), juce::Justification::centredLeft);
    g.drawText ("Complex", pad.reduced (8.0f), juce::Justification::centredRight);
    g.drawText ("Loud", pad.reduced (8.0f), juce::Justification::centredTop);
    g.drawText ("Soft", pad.reduced (8.0f), juce::Justification::centredBottom);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (pad.getCentreX(), pad.getY(), 1.0f, pad.getHeight());
    g.fillRect (pad.getX(), pad.getCentreY(), pad.getWidth(), 1.0f);

    const auto x = pad.getX() + (float) (double) region.getProperty (pl::complexity, 0.5) * pad.getWidth();
    const auto y = pad.getBottom() - (float) (double) region.getProperty (pl::loudness, 0.6) * pad.getHeight();
    g.setColour (juce::Colour (0xffe8a23a));
    g.fillEllipse (x - 9.0f, y - 9.0f, 18.0f, 18.0f);
    g.setColour (juce::Colours::white);
    g.drawEllipse (x - 9.0f, y - 9.0f, 18.0f, 18.0f, 1.5f);

    const auto right = getLocalBounds().reduced (16).withTrimmedLeft (padArea().getWidth() + 24);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const auto what = region[pl::kind].toString();
    g.drawText (what == "drums" ? "Drummer" : what == "bass" ? "Bass Player" : "Keyboard Player", right.withHeight (22), juce::Justification::centredLeft);
}

void PlayerEditor::resized()
{
    auto right = getLocalBounds().reduced (16).withTrimmedLeft (padArea().getWidth() + 24);
    right.removeFromTop (28);
    genreBox.setBounds (right.removeFromTop (26).withWidth (200));
    right.removeFromTop (10);
    auto knobs = right.removeFromTop (100);
    for (auto [slider, label] : { std::pair { &fills, &fillsLabel }, std::pair { &swing, &swingLabel } })
    {
        auto column = knobs.removeFromLeft (90);
        label->setBounds (column.removeFromTop (18));
        slider->setBounds (column);
    }
    newTake.setBounds (knobs.removeFromLeft (100).withSizeKeepingCentre (90, 28));
    right.removeFromTop (8);
    // The drum pieces in two columns.
    for (int i = 0; i < pieces.size(); ++i)
        pieces[i]->setBounds (right.getX() + (i % 3) * 140, right.getY() + (i / 3) * 24, 136, 22);
}

void PlayerEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = padArea().contains (e.getPosition()) && region.isValid();
    if (dragging)
    {
        undoManager.beginNewTransaction ("Player");
        mouseDrag (e);
    }
}

void PlayerEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const auto pad = padArea().toFloat();
    region.setProperty (pl::complexity, juce::jlimit (0.0, 1.0, (double) ((e.position.x - pad.getX()) / pad.getWidth())), &undoManager);
    region.setProperty (pl::loudness, juce::jlimit (0.0, 1.0, (double) ((pad.getBottom() - e.position.y) / pad.getHeight())), &undoManager);
    repaint();
}

void PlayerEditor::mouseUp (const juce::MouseEvent&)
{
    // The notes are written once you let go.
    if (dragging)
        pl::regenerate (region, &undoManager);
    dragging = false;
}

} // namespace riff
