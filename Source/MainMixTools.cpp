// Reset Channels (as Cubase's Reset MixConsole Channels): every channel - or the selected ones - and the master back
// to a fresh start, in one undo step.
#include "MainComponent.h"
#include "model/Project.h"

namespace riff
{

namespace
{
    class ResetChoices final : public juce::Component
    {
    public:
        explicit ResetChoices (int selectedCount)
        {
            add (levels, "Faders to 0 dB, pans centred, mute and solo off", true);
            add (strips, "Channel strips flat (EQ, dynamics), preamp and tape off", true);
            add (inserts, "Inserts switched off (kept, to switch back on)", true);
            add (removeInserts, "Inserts taken out instead", false);
            add (sends, "Sends turned all the way down", true);
            add (master, "The master too (its fader, inserts, bus compressor and tape)", true);
            add (selectedOnly, "Only the " + juce::String (selectedCount) + " selected channel" + (selectedCount == 1 ? "" : "s"), false);
            selectedOnly.setEnabled (selectedCount > 0);
            selectedOnly.onClick = [this] { master.setEnabled (! selectedOnly.getToggleState()); };
            removeInserts.onClick = [this] { if (removeInserts.getToggleState()) inserts.setToggleState (false, juce::dontSendNotification); };
            inserts.onClick = [this] { if (inserts.getToggleState()) removeInserts.setToggleState (false, juce::dontSendNotification); };
            setSize (420, 7 * 26);
        }

        model::ResetOptions options (const juce::StringArray& selected) const
        {
            model::ResetOptions o;
            o.levels = levels.getToggleState();
            o.strips = strips.getToggleState();
            o.bypassInserts = inserts.getToggleState();
            o.removeInserts = removeInserts.getToggleState();
            o.sends = sends.getToggleState();
            o.master = master.getToggleState() && ! selectedOnly.getToggleState();
            if (selectedOnly.getToggleState())
                o.onlyTracks = selected;
            return o;
        }

        void resized() override
        {
            auto area = getLocalBounds();
            for (auto* b : buttons)
                b->setBounds (area.removeFromTop (26));
        }

    private:
        void add (juce::ToggleButton& b, const juce::String& text, bool on)
        {
            b.setButtonText (text);
            b.setToggleState (on, juce::dontSendNotification);
            addAndMakeVisible (b);
            buttons.push_back (&b);
        }

        juce::ToggleButton levels, strips, inserts, removeInserts, sends, master, selectedOnly;
        std::vector<juce::ToggleButton*> buttons;
    };
}

void MainComponent::showResetChannels()
{
    const auto selected = timeline.selectedTrackIds();
    auto* window = new juce::AlertWindow ("Reset Channels",
                                          "Puts the mixer's channels back to a fresh start (Edit > Undo brings it all back).",
                                          juce::MessageBoxIconType::NoIcon);
    auto choices = std::make_shared<ResetChoices> (selected.size());
    window->addCustomComponent (choices.get());
    window->addButton ("Reset", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainComponent> (this), window, choices, selected] (int result)
    {
        window->removeChildComponent (choices.get());
        if (result != 1 || safe == nullptr)
            return;
        auto& self = *safe;
        const auto options = choices->options (selected);
        self.undoManager.beginNewTransaction ("Reset Channels");
        const auto strips = model::resetChannels (self.project, options, &self.undoManager);

        // Channel strip plugins: every parameter back to the plugin's own default
        for (const auto& strip : strips)
            if (auto* plugin = self.effectRack.getPlugin (strip[ids::id].toString()))
                for (auto* p : plugin->getParameters())
                    if (p->isAutomatable() && ! p->isMetaParameter())
                        p->setValueNotifyingHost (p->getDefaultValue());

        self.player.rebuildNow();
        self.statusLabel.setText (options.onlyTracks.isEmpty() ? "Every channel reset" + juce::String (options.master ? ", the master too" : "")
                                                               : juce::String (options.onlyTracks.size()) + " channels reset",
                                  juce::dontSendNotification);
    }), true);
}

} // namespace riff
