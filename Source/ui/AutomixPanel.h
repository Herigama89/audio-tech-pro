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

    /** Measure and apply: (genre, cycle range only, what to do). */
    std::function<void (const juce::String& genre, bool cycleOnly, automix::Options)> onRun;

    void setBusy (bool busy);
    void showResults (const std::vector<automix::Result>& results, const juce::String& genre);
    /** What matching the reference did on the master bus (or why it couldn't). */
    void showReference (const juce::StringArray& steps, const juce::String& error);

private:
    juce::ValueTree project;
    juce::ComboBox genreBox, rangeBox;
    juce::TextButton runButton { "Automix" };
    juce::ToggleButton fadersBox { "Faders" }, panBox { "Panning" }, pluginsBox { "Plugins (EQ, compression...)" };
    juce::TextEditor report;
    juce::ToggleButton referenceBox { "Sound like a reference:" };
    juce::TextButton chooseButton { "Choose a mastered song..." };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String referencePath() const;
    void updateReferenceButton();
    juce::String note;
};

} // namespace riff
