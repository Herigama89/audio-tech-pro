#pragma once

#include <JuceHeader.h>
#include "../model/Automix.h"

namespace riff
{

/**
    Mix > Automix...: choose the genre, and the faders are set to a balanced starting mix
    for it (measured from what each track plays; one undo step). The list shows what each
    track was taken to be and where its fader went.
*/
class AutomixPanel final : public juce::Component
{
public:
    explicit AutomixPanel (juce::ValueTree project);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Measure and apply: (genre, cycle range only). */
    std::function<void (const juce::String& genre, bool cycleOnly)> onRun;

    void setBusy (bool busy);
    void showResults (const std::vector<automix::Result>& results, const juce::String& genre);

private:
    juce::ValueTree project;
    juce::ComboBox genreBox, rangeBox;
    juce::TextButton runButton { "Automix" };
    juce::TextEditor report;
    juce::String note;
};

} // namespace riff
