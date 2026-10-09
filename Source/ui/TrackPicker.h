#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include "../model/Identifiers.h"

namespace riff
{

/**
    A list of tracks with a tick box each, and a button to act on the ticked
    ones. Used to import tracks from another project.
*/
class TrackPicker final : public juce::Component
{
public:
    TrackPicker (const juce::ValueTree& sourceProject, const juce::String& actionName,
                 std::function<void (const juce::StringArray&)> onChosen)
        : chosen (std::move (onChosen))
    {
        for (const auto& track : sourceProject)
        {
            if (! track.hasType (ids::TRACK))
                continue;

            auto kind = track[ids::kind].toString();
            int regions = 0;
            for (const auto& r : track)
                regions += r.hasType (ids::REGION) ? 1 : 0;

            auto* box = boxes.add (new juce::ToggleButton (track[ids::name].toString()
                                                           + "   (" + (kind == "bus" ? juce::String ("bus")
                                                                       : kind == "instrument" ? juce::String ("instrument")
                                                                                              : juce::String ("audio"))
                                                           + ", " + juce::String (regions) + (regions == 1 ? " region)" : " regions)")));
            box->setToggleState (true, juce::dontSendNotification);
            box->setColour (juce::ToggleButton::textColourId, theme::text);
            box->setColour (juce::ToggleButton::tickColourId, theme::accent);
            ids.add (track[ids::id].toString());
            list.addAndMakeVisible (box);
        }

        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        for (auto* b : { &all, &none, &go, &cancel })
            addAndMakeVisible (b);

        go.setButtonText (actionName);
        all.onClick  = [this] { for (auto* b : boxes) b->setToggleState (true, juce::dontSendNotification); };
        none.onClick = [this] { for (auto* b : boxes) b->setToggleState (false, juce::dontSendNotification); };
        cancel.onClick = [this] { close(); };
        go.onClick = [this]
        {
            juce::StringArray picked;
            for (int i = 0; i < boxes.size(); ++i)
                if (boxes[i]->getToggleState())
                    picked.add (ids[i]);
            auto callback = chosen;
            close();
            if (! picked.isEmpty() && callback)
                callback (picked);
        };

        setSize (420, juce::jlimit (180, 520, 90 + boxes.size() * 26));
    }

    void paint (juce::Graphics& g) override  { g.fillAll (theme::background); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto buttons = r.removeFromBottom (30);
        cancel.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        go.setBounds (buttons.removeFromRight (110));
        all.setBounds (buttons.removeFromLeft (60));
        buttons.removeFromLeft (6);
        none.setBounds (buttons.removeFromLeft (60));
        r.removeFromBottom (8);
        viewport.setBounds (r);

        list.setSize (r.getWidth() - viewport.getScrollBarThickness(), boxes.size() * 26);
        for (int i = 0; i < boxes.size(); ++i)
            boxes[i]->setBounds (0, i * 26, list.getWidth(), 24);
    }

private:
    void close()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    std::function<void (const juce::StringArray&)> chosen;
    juce::Component list;
    juce::Viewport viewport;
    juce::OwnedArray<juce::ToggleButton> boxes;
    juce::StringArray ids;
    juce::TextButton all { "All" }, none { "None" }, go, cancel { "Cancel" };
};

} // namespace riff
