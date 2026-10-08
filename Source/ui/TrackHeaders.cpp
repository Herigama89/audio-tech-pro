#include "TrackHeaders.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

namespace
{
    constexpr double minDb = -60.0;

    double gainToDb (double gain) { return gain <= 0.0 ? minDb : juce::jmax (minDb, juce::Decibels::gainToDecibels (gain)); }
    double dbToGain (double db)   { return db <= minDb + 0.01 ? 0.0 : juce::Decibels::decibelsToGain (db); }
}

//==============================================================================
TrackHeader::TrackHeader (juce::ValueTree t, juce::UndoManager& um, TimelineState& tl)
    : track (t), undoManager (um), timeline (tl)
{
    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.onTextChange = [this]
    {
        undoManager.beginNewTransaction ("Rename track");
        track.setProperty (ids::name, nameLabel.getText(), &undoManager);
    };
    nameLabel.addMouseListener (this, false);
    addAndMakeVisible (nameLabel);

    for (auto* b : { &muteButton, &soloButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }

    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::mute);
    soloButton.setColour (juce::TextButton::buttonOnColourId, theme::solo);
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");

    muteButton.onClick = [this]
    {
        undoManager.beginNewTransaction ("Mute");
        track.setProperty (ids::mute, ! (bool) track[ids::mute], &undoManager);
    };
    soloButton.onClick = [this]
    {
        undoManager.beginNewTransaction ("Solo");
        track.setProperty (ids::solo, ! (bool) track[ids::solo], &undoManager);
    };

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setScrollWheelEnabled (false); // let the wheel scroll the track list
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setRange (minDb, 6.0, 0.1);
    volumeSlider.setSkewFactorFromMidPoint (-12.0);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.textFromValueFunction = [] (double db) { return db <= minDb + 0.01 ? juce::String ("-inf dB") : juce::String (db, 1) + " dB"; };
    volumeSlider.onDragStart = [this] { undoManager.beginNewTransaction ("Volume"); };
    volumeSlider.onValueChange = [this] { track.setProperty (ids::volume, dbToGain (volumeSlider.getValue()), &undoManager); };
    addAndMakeVisible (volumeSlider);

    panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    panKnob.setScrollWheelEnabled (false);
    panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    panKnob.setRange (-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue (true, 0.0);
    panKnob.setPopupDisplayEnabled (true, true, nullptr);
    panKnob.textFromValueFunction = [] (double p)
    {
        const auto amount = juce::roundToInt (std::abs (p) * 64.0);
        return amount == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (amount);
    };
    panKnob.onDragStart = [this] { undoManager.beginNewTransaction ("Pan"); };
    panKnob.onValueChange = [this] { track.setProperty (ids::pan, panKnob.getValue(), &undoManager); };
    addAndMakeVisible (panKnob);

    track.addListener (this);
    refresh();
}

TrackHeader::~TrackHeader()
{
    track.removeListener (this);
}

void TrackHeader::refresh()
{
    nameLabel.setText (track[ids::name].toString(), juce::dontSendNotification);
    muteButton.setToggleState ((bool) track[ids::mute], juce::dontSendNotification);
    soloButton.setToggleState ((bool) track[ids::solo], juce::dontSendNotification);
    volumeSlider.setValue (gainToDb (track.getProperty (ids::volume, 1.0)), juce::dontSendNotification);
    panKnob.setValue (track.getProperty (ids::pan, 0.0), juce::dontSendNotification);
    repaint();
}

void TrackHeader::paint (juce::Graphics& g)
{
    const bool selected = timeline.selectedTrackId == track[ids::id].toString();

    g.fillAll (selected ? theme::selection : theme::panel);

    g.setColour (model::trackColour (track));
    g.fillRect (0, 0, 5, getHeight());

    g.setColour (theme::background);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void TrackHeader::resized()
{
    auto area = getLocalBounds().withTrimmedLeft (12).withTrimmedRight (8).withTrimmedBottom (1);

    auto top = area.removeFromTop (38).withTrimmedTop (8).withHeight (22);
    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (4);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (6);
    nameLabel.setBounds (top);

    auto bottom = area.withHeight (30);
    panKnob.setBounds (bottom.removeFromRight (30));
    bottom.removeFromRight (6);
    volumeSlider.setBounds (bottom);
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    select();

    if (e.mods.isPopupMenu())
        showMenu();
}

void TrackHeader::select()
{
    timeline.selectedTrackId = track[ids::id].toString();
    timeline.changed();
}

void TrackHeader::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Rename", [this] { nameLabel.showEditor(); });
    menu.addSeparator();
    menu.addItem ("Delete track", [t = track, &um = undoManager]
    {
        um.beginNewTransaction ("Delete track");
        model::deleteTrack (t, &um);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

//==============================================================================
TrackHeaderList::TrackHeaderList (juce::UndoManager& um, TimelineState& tl)
    : undoManager (um), timeline (tl)
{
    addAndMakeVisible (content);
    timeline.addChangeListener (this);
}

TrackHeaderList::~TrackHeaderList()
{
    timeline.removeChangeListener (this);
    project.removeListener (this);
}

void TrackHeaderList::setProject (juce::ValueTree newProject)
{
    project.removeListener (this);
    project = newProject;
    project.addListener (this);
    rebuild();
}

void TrackHeaderList::rebuild()
{
    headers.clear();

    for (const auto& track : project)
        content.addAndMakeVisible (headers.add (new TrackHeader (track, undoManager, timeline)));

    resized();
    repaint();
}

void TrackHeaderList::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Corner above the headers, level with the ruler.
    g.setColour (theme::toolbar);
    g.fillRect (0, 0, getWidth(), TimelineState::rulerHeight);
    g.setColour (theme::dimText);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (juce::String (project.getNumChildren()) + (project.getNumChildren() == 1 ? " track" : " tracks"),
                getLocalBounds().removeFromTop (TimelineState::rulerHeight).reduced (12, 0),
                juce::Justification::centredLeft);
}

void TrackHeaderList::resized()
{
    // Headers live in a child below the ruler so scrolled ones get clipped.
    content.setBounds (getLocalBounds().withTrimmedTop (TimelineState::rulerHeight));

    for (int i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, timeline.trackTop (i) - TimelineState::rulerHeight, getWidth(), TimelineState::trackHeight);
}

void TrackHeaderList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (onWheel)
        onWheel (e, wheel);
}

} // namespace riff
