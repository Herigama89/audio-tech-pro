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

    static constexpr int headerHeight = 32;

    /** The menu at the top (the track's name): everything you can do to the track and its channel strip. */
    std::function<void (juce::ValueTree track, juce::Component& target)> onTrackMenu;

    /** With no track selected: make one (0: audio, 1: instrument). */
    std::function<void (int kind)> onNewTrack;

    /** The Inspector's own options (which boxes show), for that menu. */
    void addInspectorItems (juce::PopupMenu& menu);
    static constexpr int preferredWidth = 2 * ChannelStrip::stripWidth + 3 * 4 + 12;

private:
    friend class SnapshotTool;
    /** The bar between the Region/Track boxes and the strips: drag it up or down (the strips fill the rest, their faders longer). */
    struct Divider final : juce::Component, juce::SettableTooltipClient
    {
        Divider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRect (getLocalBounds());
            g.setColour (juce::Colours::white.withAlpha (isMouseOverOrDragging() ? 0.35f : 0.12f));
            g.fillRect (getLocalBounds().withSizeKeepingCentre (28, 2));
        }
        void mouseDown (const juce::MouseEvent&) override { if (onStart) onStart(); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY()); }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }
        std::function<void()> onStart, onReset;
        std::function<void (int)> onDrag;
    };
    Divider divider;
    juce::TextButton trackMenuButton, masterMenuButton;
    juce::Label emptyLabel;   // no track selected: what to do
    juce::TextButton newAudioButton { "New Audio Track" }, newInstrumentButton { "New Instrument Track" };
    void updateHeaderButtons();
    int boxesWanted = -1, boxesAtDragStart = 0;   // -1: as tall as the boxes need
    juce::Viewport stripView;
    juce::Component stripHolder;

    void clear();

    MixerPanel& mixer;
    juce::ValueTree project, track;
    juce::Viewport viewport;
    juce::Component holder;
    std::unique_ptr<ChannelStrip> trackStrip, masterStrip;
    juce::ValueTree region;
    ParameterBox regionBox { mixer.getUndoManager() }, trackBox { mixer.getUndoManager() };
    void buildTrackBox();
    void buildRegionBox();
    juce::ValueTree regionBoxTrack;   // the track the Region box was built for (its defaults)

    /** The track's Flex Mode greys the region's Flex rows in or out. */
    struct TrackWatcher final : juce::ValueTree::Listener
    {
        std::function<void()> onChange;
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& p) override { if (p == ids::flexMode && onChange) onChange(); }
    } trackWatcher;
    int boxesHeight() const;
};

} // namespace riff
