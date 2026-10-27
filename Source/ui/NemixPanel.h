#pragma once

#include <JuceHeader.h>

namespace riff
{
class Nemix;

/**
    Nemix's window: the oscillators with their wavetables, sub and noise, the filters, envelopes, LFOs and
    macros (drag any of them onto a knob to modulate it), the matrix, the effects, the arpeggiator and voicing,
    and the sound browser - on a graffiti wall, in the colours of its logo.

    Every control writes (undoably) to the track's NEMIX tree; the synth hears it straight away.
*/
class NemixPanel final : public juce::Component,
                         public juce::DragAndDropContainer
{
public:
    NemixPanel (juce::ValueTree track, juce::UndoManager& um, std::function<std::shared_ptr<Nemix>()> liveSynth);
    ~NemixPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Shows a page: 0 sound, 1 matrix, 2 effects, 3 arp and voicing, 4 browser. */
    void showPage (int page);
    /** Loads a factory sound by name (for tests and snapshots). */
    bool loadPresetNamed (const juce::String& name);

    struct Impl;
private:
    std::unique_ptr<Impl> impl;
};

} // namespace riff
