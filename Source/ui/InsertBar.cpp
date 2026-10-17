#include "InsertBar.h"
#include "InstrumentWindows.h"
#include "Theme.h"
#include "../model/Project.h"

namespace riff
{

InsertBar::InsertBar (juce::ValueTree i, juce::ValueTree p, juce::UndoManager& um, bool acceptsSidechain)
    : insert (std::move (i)), project (std::move (p)), undoManager (um), sidechainable (acceptsSidechain)
{
    for (auto* b : { &pinButton, &bypassButton, &sideButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    pinButton.setClickingTogglesState (true);
    pinButton.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    pinButton.setTooltip ("Pin: the window stays above the main window and open while you work (and the next plugin window opens pinned too)");
    pinButton.onClick = [this]
    {
        EditorWindow::pinByDefault = pinButton.getToggleState();
        if (auto* window = findParentComponentOfClass<EditorWindow>())
            window->setPinned (pinButton.getToggleState());
    };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe9b949));
    bypassButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    bypassButton.setTooltip ("Bypass: take this plugin out of the signal (Option-click an insert slot does the same)");
    bypassButton.onClick = [this]
    {
        undoManager.beginNewTransaction ((bypassButton.getToggleState() ? "Bypass " : "Activate ") + insert[ids::name].toString());
        insert.setProperty (ids::bypass, bypassButton.getToggleState(), &undoManager);
    };

    {
        const auto owner = model::findById (project, insert.getParent()[ids::owner].toString());
        printable = owner.isValid() && ! model::isBusTrack (owner) && ! model::isInstrumentTrack (owner) && ! model::isVcaTrack (owner);
    }
    printButton.setClickingTogglesState (true);
    printButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd8352d));
    printButton.setTooltip ("Print While Recording (as in LUNA): the input is recorded through this plugin, so the recording has it; "
                            "you hear it as you play, and on playback it's left out (it's in the audio already)");
    printButton.onClick = [this]
    {
        undoManager.beginNewTransaction (printButton.getToggleState() ? "Print While Recording" : "Don't Print");
        insert.setProperty (ids::print, printButton.getToggleState(), &undoManager);
    };
    printButton.setWantsKeyboardFocus (false);
    addChildComponent (printButton);
    printButton.setVisible (printable);

    sideButton.setTooltip (sidechainable ? "Side Chain: another track's output keys this plugin (its detector hears that track)"
                                         : "This plugin has no side-chain input");
    sideButton.onClick = [this]
    {
        juce::PopupMenu menu;
        addSidechainItems (menu, insert, project, undoManager);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sideButton));
    };
    sideButton.setEnabled (sidechainable);

    insert.addListener (this);
    refresh();
}

InsertBar::~InsertBar()
{
    insert.removeListener (this);
}

void InsertBar::addSidechainItems (juce::PopupMenu& menu, juce::ValueTree insert, juce::ValueTree project, juce::UndoManager& um)
{
    const auto current = insert[ids::sidechain].toString();
    const auto owner = insert.getParent()[ids::owner].toString();
    auto set = [insert, &um] (const juce::String& id, const juce::String& name) mutable
    {
        um.beginNewTransaction (id.isEmpty() ? juce::String ("No Side Chain") : "Side Chain from " + name);
        insert.setProperty (ids::sidechain, id, &um);
    };
    menu.addSectionHeader ("Side Chain");
    menu.addItem ("None", true, current.isEmpty(), [set] () mutable { set ({}, {}); });
    menu.addSeparator();
    for (const auto& track : project)
    {
        if (! track.hasType (ids::TRACK) || model::isVcaTrack (track))
            continue;
        const auto id = track[ids::id].toString();
        if (id == owner)
            continue;
        const auto name = track[ids::name].toString();
        menu.addItem ((model::isBusTrack (track) ? "Bus: " : "") + name, true, id == current, [set, id, name] () mutable { set (id, name); });
    }
}

void InsertBar::refresh()
{
    bypassButton.setToggleState ((bool) insert[ids::bypass], juce::dontSendNotification);
    printButton.setToggleState ((bool) insert[ids::print], juce::dontSendNotification);
    if (auto* window = findParentComponentOfClass<EditorWindow>())
        pinButton.setToggleState (window->isPinned(), juce::dontSendNotification);
    const auto source = model::findById (project, insert[ids::sidechain].toString());
    sideButton.setButtonText ("Side Chain: " + (source.isValid() ? source[ids::name].toString() : juce::String ("None")));
    sideButton.setToggleState (source.isValid(), juce::dontSendNotification);
}

void InsertBar::resized()
{
    auto area = getLocalBounds();
    pinButton.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (6);
    bypassButton.setBounds (area.removeFromLeft (70));
    area.removeFromLeft (6);
    if (printable)
    {
        printButton.setBounds (area.removeFromLeft (60));
        area.removeFromLeft (6);
    }
    sideButton.setBounds (area);
}

} // namespace riff
