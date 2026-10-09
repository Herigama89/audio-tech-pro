#pragma once

#include <JuceHeader.h>
#include "Mixer.h"
#include "ParameterBox.h"

namespace riff
{

/**
    Logic's Inspector (I): down the left of the window, the selected track's
    channel strip next to the master's, so you can mix without opening the mixer.
*/
class Inspector final : public juce::Component
{
public:
    explicit Inspector (MixerPanel& mixer);
    ~Inspector() override;

    /** Shows this track's strip (rebuilt only when the track changes). */
    void setTrack (juce::ValueTree project, juce::ValueTree track);

    /** The Region box shows this region's parameters (or nothing). */
    void setRegion (juce::ValueTree region);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerHeight = 26;
    static constexpr int preferredWidth = 2 * ChannelStrip::stripWidth + 3 * 4 + 12;

private:
    void clear();

    MixerPanel& mixer;
    juce::ValueTree project, track;
    juce::Viewport viewport;
    juce::Component holder;
    std::unique_ptr<ChannelStrip> trackStrip, masterStrip;
    juce::ValueTree region;
    ParameterBox regionBox { mixer.getUndoManager() }, trackBox { mixer.getUndoManager() };
    void buildTrackBox();
    int boxesHeight() const;
};

} // namespace riff
