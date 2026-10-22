#include "InsertBar.h"
#include "InstrumentWindows.h"
#include "Theme.h"
#include "../model/Project.h"
#include "../model/Presets.h"

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

    presetButton.setTooltip ("Presets: save this plug-in's settings, load one, or Set as Default so every new one starts that way");
    presetButton.onClick = [this] { showPresetMenu(); };
    previousButton.setTooltip ("The previous preset");
    nextButton.setTooltip ("The next preset");
    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    for (auto* b : { &presetButton, &previousButton, &nextButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    insert.addListener (this);
    refresh();
}

juce::String InsertBar::currentState() const
{
    return captureState != nullptr ? captureState (insert) : juce::String();
}

void InsertBar::loadPreset (const juce::String& name)
{
    undoManager.beginNewTransaction ("Load Preset " + name);
    juce::String state;
    const auto ok = name == "Default" ? presets::applyDefault (insert, &undoManager, &state)
                                      : presets::load (insert, name, &undoManager, &state);
    if (ok && state.isNotEmpty() && applyState != nullptr)
        applyState (insert, state);
}

void InsertBar::stepPreset (int direction)
{
    const auto names = presets::list (insert);
    if (names.isEmpty())
        return;
    const auto at = names.indexOf (insert["presetName"].toString());
    const auto next = at < 0 ? (direction > 0 ? 0 : names.size() - 1) : (at + direction + names.size()) % names.size();
    loadPreset (names[next]);
}

void InsertBar::showPresetMenu()
{
    const auto names = presets::list (insert);
    const auto current = insert["presetName"].toString();
    const auto plugin = presets::displayNameOf (insert);
    juce::PopupMenu menu;
    menu.addItem ("Save Preset...", [this]
    {
        auto* box = new juce::AlertWindow ("Save Preset", "A name for these settings:", juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", insert["presetName"].toString().isNotEmpty() && insert["presetName"].toString() != "Default"
                                        && insert["presetName"].toString() != "Factory Default" ? insert["presetName"].toString() : juce::String());
        box->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, box] (int result)
        {
            const auto name = box->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
            {
                if (presets::save (insert, name, currentState()))
                    insert.setProperty ("presetName", name, nullptr);
            }
        }), true);
    });
    menu.addItem ("Set as Default for " + plugin, [this]
    {
        presets::saveDefault (insert, currentState());
        insert.setProperty ("presetName", "Default", nullptr);
    });
    menu.addItem ("Load Default", presets::hasDefault (insert), false, [this] { loadPreset ("Default"); });
    menu.addItem ("Clear Default (start from the factory settings)", presets::hasDefault (insert), false, [this] { presets::clearDefault (insert); });
    if (insert[ids::kind].toString() != model::pluginKind)
        menu.addItem ("Reset to Factory Default", [this]
        {
            undoManager.beginNewTransaction ("Reset " + presets::displayNameOf (insert));
            presets::resetToFactory (insert, &undoManager);
        });
    menu.addSeparator();
    if (names.isEmpty())
        menu.addItem ("(no presets saved yet)", false, false, [] {});
    for (const auto& n : names)
        menu.addItem (n, true, n == current, [this, n] { loadPreset (n); });
    if (! names.isEmpty())
    {
        juce::PopupMenu remove;
        for (const auto& n : names)
            remove.addItem (n, [this, n] { presets::remove (insert, n); });
        menu.addSeparator();
        menu.addSubMenu ("Delete Preset", remove);
    }
    menu.addItem ("Show Presets Folder", [this]
    {
        auto folder = presets::root().getChildFile (presets::keyOf (insert));
        folder.createDirectory();
        folder.revealToUser();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton));
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
    const auto preset = insert["presetName"].toString();
    presetButton.setButtonText ((preset.isNotEmpty() ? preset : juce::String ("Preset")) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
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
    previousButton.setBounds (area.removeFromLeft (22));
    area.removeFromLeft (2);
    presetButton.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (2);
    nextButton.setBounds (area.removeFromLeft (22));
    area.removeFromLeft (6);
    sideButton.setBounds (area);
}

} // namespace riff
