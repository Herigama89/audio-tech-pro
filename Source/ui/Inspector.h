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
    static constexpr int minWidth = 150, maxWidth = 460;

    /** Drag its right edge to make it wider or narrower (the strips grow with it). */
    int getWidthWanted() const noexcept { return widthWanted; }
    void setWidthWanted (int w) { widthWanted = juce::jlimit (minWidth, maxWidth, w); }
    std::function<void (int newWidth)> onWidthChanged;

private:
    struct Edge final : juce::Component, juce::SettableTooltipClient
    {
        Edge() { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::white.withAlpha (isMouseOverOrDragging() ? 0.25f : 0.06f));
            g.fillRect (getLocalBounds().withSizeKeepingCentre (2, getHeight()));
        }
        void mouseDown (const juce::MouseEvent&) override { if (onStart) onStart(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartX()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
        std::function<void()> onStart, onReset;
        std::function<void (int)> onDrag;
    };
    Edge edge;
    int widthWanted = preferredWidth, widthAtDragStart = preferredWidth;

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
