#include "CombineInterfacesPanel.h"
#include "Theme.h"

namespace riff
{

CombineInterfacesPanel::CombineInterfacesPanel (const aggregate::Setup& current, bool combinedNow,
                                                std::function<juce::Array<aggregate::Interface>()> listInterfaces,
                                                std::function<void (const aggregate::Setup&)> onCombine,
                                                std::function<void()> onSeparate)
    : lister (std::move (listInterfaces)), ticked (current.uids), combined (combinedNow)
{
    intro.setText (aggregate::isSupported()
                       ? "Tick the interfaces to use together. Their inputs and outputs become one list, the clock's first. "
                         "Tracks can then record from any of them; playback comes out of the clock interface's outputs 1-2."
                       : aggregate::create ({}).isNotEmpty() ? aggregate::create ({}) : juce::String(),
                   juce::dontSendNotification);
    intro.setFont (juce::FontOptions (12.5f));
    intro.setColour (juce::Label::textColourId, theme::dimText);
    intro.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (intro);

    clockCaption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (clockCaption);
    clockBox.onChange = [this] { update(); };
    clockBox.setTooltip ("The interface the others follow. Pick your main one.");
    addAndMakeVisible (clockBox);

    driftButton.setToggleState (current.driftCorrection, juce::dontSendNotification);
    addAndMakeVisible (driftButton);

    summary.setFont (juce::FontOptions (12.5f));
    summary.setColour (juce::Label::textColourId, theme::cream);
    summary.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (summary);

    combineButton.setColour (juce::TextButton::buttonColourId, theme::goldDark);
    combineButton.onClick = [this, onCombine] { if (onCombine) onCombine (getSetup()); };
    separateButton.onClick = [this, onSeparate] { if (onSeparate) onSeparate(); };
    refreshButton.onClick = [this] { refresh(); };
    for (auto* b : { &combineButton, &separateButton, &refreshButton })
        addAndMakeVisible (b);

    refresh();
    if (current.clockUid.isNotEmpty())
        for (int i = 0; i < interfaces.size(); ++i)
            if (interfaces[i].uid == current.clockUid)
                clockBox.setSelectedId (i + 1, juce::dontSendNotification);
    update();

    setSize (560, 250 + interfaces.size() * 28);
}

void CombineInterfacesPanel::refresh()
{
    interfaces = lister ? lister() : juce::Array<aggregate::Interface>();
    ticks.clear();

    for (const auto& i : interfaces)
    {
        auto text = i.name + "   " + juce::String (i.inputs) + " in, " + juce::String (i.outputs) + " out" + (i.isUsb ? "  (USB)" : "");
        auto* t = ticks.add (new juce::ToggleButton (text));
        t->setToggleState (ticked.contains (i.uid), juce::dontSendNotification);
        t->onClick = [this, uid = i.uid, t]
        {
            if (t->getToggleState())
                ticked.addIfNotAlreadyThere (uid);
            else
                ticked.removeString (uid);
            update();
        };
        addAndMakeVisible (t);
    }

    if (interfaces.isEmpty())
        summary.setText (aggregate::isSupported() ? "No audio interfaces found." : juce::String(), juce::dontSendNotification);

    update();
    resized();
}

aggregate::Setup CombineInterfacesPanel::getSetup() const
{
    aggregate::Setup s;
    for (const auto& i : interfaces)          // in the order the Mac lists them
        if (ticked.contains (i.uid))
            s.uids.add (i.uid);

    const auto clock = clockBox.getSelectedId() - 1;
    s.clockUid = juce::isPositiveAndBelow (clock, interfaces.size()) && s.uids.contains (interfaces[clock].uid)
                     ? interfaces[clock].uid : s.uids[0];
    s.driftCorrection = driftButton.getToggleState();
    return s;
}

void CombineInterfacesPanel::update()
{
    // The clock can be any ticked interface.
    const auto previous = clockBox.getSelectedId();
    clockBox.clear (juce::dontSendNotification);
    for (int i = 0; i < interfaces.size(); ++i)
        if (ticked.contains (interfaces[i].uid))
            clockBox.addItem (interfaces[i].name, i + 1);
    if (clockBox.indexOfItemId (previous) >= 0)
        clockBox.setSelectedId (previous, juce::dontSendNotification);
    else if (clockBox.getNumItems() > 0)
        clockBox.setSelectedItemIndex (0, juce::dontSendNotification);

    const auto setup = getSetup();
    combineButton.setEnabled (setup.isActive() && aggregate::isSupported());
    separateButton.setEnabled (combined);

    if (! interfaces.isEmpty())
    {
        juce::String text;
        int in = 1, out = 1;
        for (const auto& uid : setup.orderedUids())
            for (const auto& i : interfaces)
                if (i.uid == uid)
                {
                    text << i.name << ":  inputs " << in << "-" << (in + i.inputs - 1)
                         << ",  outputs " << out << "-" << (out + i.outputs - 1) << (uid == setup.clockUid ? "   (clock)" : "") << "\n";
                    in += i.inputs;
                    out += i.outputs;
                }
        if (! setup.isActive())
            text = "Tick two or more interfaces.";
        summary.setText (text, juce::dontSendNotification);
    }
}

void CombineInterfacesPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void CombineInterfacesPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    intro.setBounds (area.removeFromTop (48));
    area.removeFromTop (6);

    for (auto* t : ticks)
        t->setBounds (area.removeFromTop (26));
    area.removeFromTop (8);

    auto row = area.removeFromTop (26);
    clockCaption.setBounds (row.removeFromLeft (56));
    clockBox.setBounds (row.removeFromLeft (240));
    area.removeFromTop (6);
    driftButton.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto buttons = area.removeFromBottom (32);
    combineButton.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    separateButton.setBounds (buttons.removeFromRight (150));
    refreshButton.setBounds (buttons.removeFromLeft (110));
    area.removeFromBottom (8);
    summary.setBounds (area);
}

} // namespace riff
