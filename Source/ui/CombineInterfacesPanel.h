#pragma once

#include <JuceHeader.h>
#include "engine/AggregateDevice.h"

namespace riff
{

/** The Combine Interfaces window: tick the interfaces to use together and choose the clock. */
class CombineInterfacesPanel final : public juce::Component
{
public:
    CombineInterfacesPanel (const aggregate::Setup& current, bool combinedNow,
                            std::function<juce::Array<aggregate::Interface>()> listInterfaces,
                            std::function<void (const aggregate::Setup&)> onCombine,
                            std::function<void()> onSeparate);

    aggregate::Setup getSetup() const;

    /** Looks for interfaces again (after plugging one in). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void update();

    std::function<juce::Array<aggregate::Interface>()> lister;
    juce::Array<aggregate::Interface> interfaces;
    juce::OwnedArray<juce::ToggleButton> ticks;
    juce::StringArray ticked;

    juce::Label intro, clockCaption { {}, "Clock" }, summary;
    juce::ComboBox clockBox;
    juce::ToggleButton driftButton { "Drift correction on the other interfaces (needed unless they share a word clock cable)" };
    juce::TextButton combineButton { "Combine" }, separateButton { "Use one interface" }, refreshButton { "Look again" };
    bool combined = false;
};

} // namespace riff
